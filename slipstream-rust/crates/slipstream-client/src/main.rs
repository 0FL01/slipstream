mod dns;
mod error;
mod flow_relay;
mod pacing;
mod pinning;
mod runtime;
mod streams;
mod tcp_bridge;

use clap::{parser::ValueSource, ArgGroup, CommandFactory, FromArgMatches, Parser};
use slipstream_core::{
    normalize_domain, parse_host_port, parse_host_port_parts, sip003, AddressKind, HostPort,
};
use slipstream_ffi::{ClientConfig, ResolverMode, ResolverSpec};
use tokio::runtime::Builder;
use tracing_subscriber::EnvFilter;

use flow_relay::read_bootstrap;
use runtime::run_client;

#[derive(Parser, Debug)]
#[command(
    name = "slipstream-client",
    about = "slipstream-client - A high-performance covert channel over DNS (client)",
    group(
        ArgGroup::new("resolvers")
            .multiple(true)
            .args(["resolver", "authoritative", "authoritative_tcp"])
    )
)]
struct Args {
    #[arg(long = "tcp-listen-host", default_value = "::")]
    tcp_listen_host: String,
    #[arg(long = "tcp-listen-port", short = 'l', default_value_t = 5201)]
    tcp_listen_port: u16,
    #[arg(long = "resolver", short = 'r', value_parser = parse_resolver)]
    resolver: Vec<HostPort>,
    #[arg(
        long = "congestion-control",
        short = 'c',
        value_parser = ["bbr", "dcubic"]
    )]
    congestion_control: Option<String>,
    #[arg(long = "authoritative", value_parser = parse_resolver)]
    authoritative: Vec<HostPort>,
    /// Authoritative mode through a recursive resolver over DNS-over-TCP (in-process bridge).
    #[arg(long = "authoritative-tcp", value_parser = parse_resolver)]
    authoritative_tcp: Vec<HostPort>,
    /// Aggregate TCP connections across all --authoritative-tcp paths.
    #[arg(long = "tcp-lanes", default_value_t = 56)]
    tcp_lanes: usize,
    /// Outstanding exchanges per TCP connection.
    #[arg(long = "tcp-depth", default_value_t = 1)]
    tcp_depth: usize,
    /// Aggregate pending-query queue across all --authoritative-tcp paths.
    #[arg(long = "tcp-queue", default_value_t = 64)]
    tcp_queue: usize,
    #[arg(
        short = 'g',
        long = "gso",
        num_args = 0..=1,
        default_value_t = false,
        default_missing_value = "true"
    )]
    gso: bool,
    #[arg(long = "domain", short = 'd', value_parser = parse_domain)]
    domain: Option<String>,
    #[arg(long = "cert", value_name = "PATH")]
    cert: Option<String>,
    #[arg(long = "keep-alive-interval", short = 't', default_value_t = 400)]
    keep_alive_interval: u16,
    #[arg(long = "idle-keep-alive-interval", default_value_t = 5000)]
    idle_keep_alive_interval: u16,
    #[arg(long = "flow-relay-stdin")]
    flow_relay_stdin: bool,
    #[arg(long = "dpi-stats")]
    dpi_stats: bool,
    #[arg(long = "debug-poll")]
    debug_poll: bool,
    #[arg(long = "debug-streams")]
    debug_streams: bool,
}

fn main() {
    init_logging();
    let matches = Args::command().get_matches();
    let args = Args::from_arg_matches(&matches).unwrap_or_else(|err| err.exit());
    let sip003_env = sip003::read_sip003_env().unwrap_or_else(|err| {
        tracing::error!("SIP003 env error: {}", err);
        std::process::exit(2);
    });
    if sip003_env.is_present() {
        tracing::info!("SIP003 env detected; applying SS_* overrides with CLI precedence");
    }

    let tcp_listen_host_provided = cli_provided(&matches, "tcp_listen_host");
    let tcp_listen_port_provided = cli_provided(&matches, "tcp_listen_port");
    let (tcp_listen_host, tcp_listen_port) = sip003::select_host_port(
        &args.tcp_listen_host,
        args.tcp_listen_port,
        tcp_listen_host_provided,
        tcp_listen_port_provided,
        sip003_env.local_host.as_deref(),
        sip003_env.local_port.as_deref(),
        "SS_LOCAL",
    )
    .unwrap_or_else(|err| {
        tracing::error!("SIP003 env error: {}", err);
        std::process::exit(2);
    });

    let domain = if let Some(domain) = args.domain.clone() {
        domain
    } else {
        let option_domain = parse_domain_option(&sip003_env.plugin_options).unwrap_or_else(|err| {
            tracing::error!("SIP003 env error: {}", err);
            std::process::exit(2);
        });
        if let Some(domain) = option_domain {
            domain
        } else {
            tracing::error!("A domain is required");
            std::process::exit(2);
        }
    };

    let cli_has_resolvers = has_cli_resolvers(&matches);
    let resolvers = if cli_has_resolvers {
        let bridge = (args.tcp_lanes, args.tcp_depth, args.tcp_queue);
        build_resolvers(&matches, true, bridge).unwrap_or_else(|err| {
            tracing::error!("Resolver error: {}", err);
            std::process::exit(2);
        })
    } else {
        let resolver_options = parse_resolvers_from_options(&sip003_env.plugin_options)
            .unwrap_or_else(|err| {
                tracing::error!("SIP003 env error: {}", err);
                std::process::exit(2);
            });
        if !resolver_options.resolvers.is_empty() {
            resolver_options.resolvers
        } else {
            let sip003_remote = sip003::parse_endpoint(
                sip003_env.remote_host.as_deref(),
                sip003_env.remote_port.as_deref(),
                "SS_REMOTE",
            )
            .unwrap_or_else(|err| {
                tracing::error!("SIP003 env error: {}", err);
                std::process::exit(2);
            });
            if let Some(endpoint) = &sip003_remote {
                let mode = if resolver_options.authoritative_remote {
                    ResolverMode::Authoritative
                } else {
                    ResolverMode::Recursive
                };
                let resolver =
                    parse_host_port_parts(&endpoint.host, endpoint.port, AddressKind::Resolver)
                        .unwrap_or_else(|err| {
                            tracing::error!("SIP003 env error: {}", err);
                            std::process::exit(2);
                        });
                vec![ResolverSpec { resolver, mode, upstream: None }]
            } else {
                tracing::error!("At least one resolver is required");
                std::process::exit(2);
            }
        }
    };

    let congestion_control = if args.congestion_control.is_some() {
        args.congestion_control.clone()
    } else {
        parse_congestion_control(&sip003_env.plugin_options).unwrap_or_else(|err| {
            tracing::error!("SIP003 env error: {}", err);
            std::process::exit(2);
        })
    };

    let cert = if args.cert.is_some() {
        args.cert.clone()
    } else {
        sip003::last_option_value(&sip003_env.plugin_options, "cert")
    };
    if cert.is_none() {
        tracing::warn!(
            "Server certificate pinning is disabled; this allows MITM. Provide --cert to pin the server leaf, or dismiss this if your underlying tunnel provides authentication."
        );
    }

    let keep_alive_interval = if cli_provided(&matches, "keep_alive_interval") {
        args.keep_alive_interval
    } else {
        let keep_alive_override = parse_keep_alive_interval(&sip003_env.plugin_options)
            .unwrap_or_else(|err| {
                tracing::error!("SIP003 env error: {}", err);
                std::process::exit(2);
            });
        keep_alive_override.unwrap_or(args.keep_alive_interval)
    };

    let flow_relay = if args.flow_relay_stdin {
        if tcp_listen_host != "127.0.0.1" {
            tracing::error!("FlowRelay requires --tcp-listen-host 127.0.0.1");
            std::process::exit(2);
        }
        Some(std::sync::Arc::new(
            read_bootstrap(std::io::stdin().lock()).unwrap_or_else(|err| {
                tracing::error!("FlowRelay bootstrap error: {}", err);
                std::process::exit(2);
            }),
        ))
    } else {
        None
    };

    let dns_key = flow_relay.as_ref().map(|relay| relay.dns_key());
    let config = ClientConfig {
        tcp_listen_host: &tcp_listen_host,
        tcp_listen_port,
        resolvers: &resolvers,
        congestion_control: congestion_control.as_deref(),
        gso: args.gso,
        domain: &domain,
        cert: cert.as_deref(),
        keep_alive_interval: keep_alive_interval as usize,
        idle_keep_alive_interval: args.idle_keep_alive_interval as usize,
        dns_key,
        dpi_stats: args.dpi_stats,
        debug_poll: args.debug_poll,
        debug_streams: args.debug_streams,
    };

    let runtime = Builder::new_current_thread()
        .enable_io()
        .enable_time()
        .build()
        .expect("Failed to build Tokio runtime");
    match runtime.block_on(run_client(&config, flow_relay)) {
        Ok(code) => std::process::exit(code),
        Err(err) => {
            tracing::error!("Client error: {}", err);
            std::process::exit(1);
        }
    }
}

fn init_logging() {
    let filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"));
    let _ = tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_target(false)
        .without_time()
        .try_init();
}

fn parse_domain(input: &str) -> Result<String, String> {
    normalize_domain(input).map_err(|err| err.to_string())
}

fn parse_resolver(input: &str) -> Result<HostPort, String> {
    parse_host_port(input, 53, AddressKind::Resolver).map_err(|err| err.to_string())
}

fn build_resolvers(
    matches: &clap::ArgMatches,
    require: bool,
    (lanes, depth, queue): (usize, usize, usize),
) -> Result<Vec<ResolverSpec>, String> {
    let mut ordered = Vec::new();
    collect_resolvers(matches, "resolver", ResolverMode::Recursive, &mut ordered)?;
    collect_resolvers(
        matches,
        "authoritative",
        ResolverMode::Authoritative,
        &mut ordered,
    )?;
    let mut tcp = Vec::new();
    collect_resolvers(
        matches,
        "authoritative_tcp",
        ResolverMode::Authoritative,
        &mut tcp,
    )?;
    if !tcp.is_empty() {
        let paths = tcp.len();
        if lanes < paths || queue < paths || depth == 0 {
            return Err("--tcp-lanes/--tcp-queue must cover every TCP path; --tcp-depth > 0".into());
        }
        for (idx, spec) in tcp {
            let target = resolve_socket_addr(&spec.resolver)?;
            let config = tcp_bridge::BridgeConfig {
                lanes: lanes / paths,
                depth,
                queue: queue / paths,
            };
            let local = tcp_bridge::spawn(target, config).map_err(|err| err.to_string())?;
            let resolver = parse_host_port(&local.to_string(), 53, AddressKind::Resolver)
                .map_err(|err| err.to_string())?;
            ordered.push((idx, ResolverSpec { resolver, mode: spec.mode, upstream: Some(spec.resolver) }));
        }
    }
    if ordered.is_empty() && require {
        return Err("At least one resolver is required".to_string());
    }
    ordered.sort_by_key(|(idx, _)| *idx);
    Ok(ordered.into_iter().map(|(_, spec)| spec).collect())
}

fn collect_resolvers(
    matches: &clap::ArgMatches,
    name: &str,
    mode: ResolverMode,
    ordered: &mut Vec<(usize, ResolverSpec)>,
) -> Result<(), String> {
    let indices: Vec<usize> = matches.indices_of(name).into_iter().flatten().collect();
    let values: Vec<HostPort> = matches
        .get_many::<HostPort>(name)
        .into_iter()
        .flatten()
        .cloned()
        .collect();
    if indices.len() != values.len() {
        return Err(format!("Mismatched {} arguments", name));
    }
    for (idx, resolver) in indices.into_iter().zip(values) {
        ordered.push((idx, ResolverSpec { resolver, mode, upstream: None }));
    }
    Ok(())
}

fn resolve_socket_addr(host_port: &HostPort) -> Result<std::net::SocketAddr, String> {
    use std::net::ToSocketAddrs;
    (host_port.host.as_str(), host_port.port)
        .to_socket_addrs()
        .map_err(|err| err.to_string())?
        .next()
        .ok_or_else(|| format!("Cannot resolve {}", host_port.host))
}

fn cli_provided(matches: &clap::ArgMatches, id: &str) -> bool {
    matches.value_source(id) == Some(ValueSource::CommandLine)
}

fn has_cli_resolvers(matches: &clap::ArgMatches) -> bool {
    matches
        .get_many::<HostPort>("resolver")
        .map(|values| values.len() > 0)
        .unwrap_or(false)
        || matches
            .get_many::<HostPort>("authoritative")
            .map(|values| values.len() > 0)
            .unwrap_or(false)
        || matches
            .get_many::<HostPort>("authoritative_tcp")
            .map(|values| values.len() > 0)
            .unwrap_or(false)
}

fn parse_domain_option(options: &[sip003::Sip003Option]) -> Result<Option<String>, String> {
    let mut domain = None;
    let mut saw_domain = false;
    for option in options {
        if option.key == "domain" {
            if saw_domain {
                return Err("SIP003 domain option must not be repeated".to_string());
            }
            saw_domain = true;
            let mut entries = sip003::split_list(&option.value).map_err(|err| err.to_string())?;
            if entries.len() > 1 {
                return Err("SIP003 domain option must contain a single value".to_string());
            }
            let entry = entries
                .pop()
                .ok_or_else(|| "SIP003 domain option must contain a single value".to_string())?;
            let normalized = normalize_domain(&entry).map_err(|err| err.to_string())?;
            domain = Some(normalized);
        }
    }
    Ok(domain)
}

struct ResolverOptions {
    resolvers: Vec<ResolverSpec>,
    authoritative_remote: bool,
}

fn parse_resolvers_from_options(
    options: &[sip003::Sip003Option],
) -> Result<ResolverOptions, String> {
    let mut ordered = Vec::new();
    let mut authoritative_remote = false;
    for option in options {
        let mode = match option.key.as_str() {
            "resolver" => ResolverMode::Recursive,
            "authoritative" => ResolverMode::Authoritative,
            _ => continue,
        };
        let trimmed = option.value.trim();
        if trimmed.is_empty() {
            if mode == ResolverMode::Authoritative {
                authoritative_remote = true;
                continue;
            }
            return Err("Empty resolver value is not allowed".to_string());
        }
        let entries = sip003::split_list(&option.value).map_err(|err| err.to_string())?;
        for entry in entries {
            let resolver = parse_host_port(&entry, 53, AddressKind::Resolver)
                .map_err(|err| err.to_string())?;
            ordered.push(ResolverSpec { resolver, mode, upstream: None });
        }
    }
    Ok(ResolverOptions {
        resolvers: ordered,
        authoritative_remote,
    })
}

fn parse_congestion_control(options: &[sip003::Sip003Option]) -> Result<Option<String>, String> {
    let mut last = None;
    for option in options {
        if option.key == "congestion-control" {
            let value = option.value.trim();
            if value != "bbr" && value != "dcubic" {
                return Err(format!("Invalid congestion-control value: {}", value));
            }
            last = Some(value.to_string());
        }
    }
    Ok(last)
}

fn parse_keep_alive_interval(options: &[sip003::Sip003Option]) -> Result<Option<u16>, String> {
    let mut last = None;
    for option in options {
        if option.key == "keep-alive-interval" {
            let value = option.value.trim();
            let parsed = value
                .parse::<u16>()
                .map_err(|_| format!("Invalid keep-alive-interval value: {}", value))?;
            last = Some(parsed);
        }
    }
    Ok(last)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_ordered_resolvers() {
        let matches = Args::command()
            .try_get_matches_from([
                "slipstream-client",
                "--domain",
                "example.com",
                "--resolver",
                "1.1.1.1",
                "--authoritative",
                "2.2.2.2",
                "--resolver",
                "3.3.3.3:5353",
            ])
            .expect("matches should parse");
        let resolvers = build_resolvers(&matches, true, (56, 1, 64)).expect("resolvers should parse");
        assert_eq!(resolvers.len(), 3);
        assert_eq!(resolvers[0].resolver.host, "1.1.1.1");
        assert_eq!(resolvers[0].resolver.port, 53);
        assert_eq!(resolvers[0].mode, ResolverMode::Recursive);
        assert_eq!(resolvers[1].resolver.host, "2.2.2.2");
        assert_eq!(resolvers[1].mode, ResolverMode::Authoritative);
        assert_eq!(resolvers[2].resolver.host, "3.3.3.3");
        assert_eq!(resolvers[2].resolver.port, 5353);
    }

    #[test]
    fn maps_authoritative_first() {
        let matches = Args::command()
            .try_get_matches_from([
                "slipstream-client",
                "--domain",
                "example.com",
                "--authoritative",
                "8.8.8.8",
                "--resolver",
                "9.9.9.9",
            ])
            .expect("matches should parse");
        let resolvers = build_resolvers(&matches, true, (56, 1, 64)).expect("resolvers should parse");
        assert_eq!(resolvers.len(), 2);
        assert_eq!(resolvers[0].resolver.host, "8.8.8.8");
        assert_eq!(resolvers[0].mode, ResolverMode::Authoritative);
        assert_eq!(resolvers[1].resolver.host, "9.9.9.9");
        assert_eq!(resolvers[1].mode, ResolverMode::Recursive);
    }

    #[test]
    fn parses_plugin_resolvers_in_order() {
        let options = vec![
            sip003::Sip003Option {
                key: "resolver".to_string(),
                value: "1.1.1.1,2.2.2.2:5353".to_string(),
            },
            sip003::Sip003Option {
                key: "authoritative".to_string(),
                value: "3.3.3.3".to_string(),
            },
            sip003::Sip003Option {
                key: "resolver".to_string(),
                value: "4.4.4.4".to_string(),
            },
        ];
        let parsed = parse_resolvers_from_options(&options).expect("options should parse");
        assert_eq!(parsed.resolvers.len(), 4);
        assert_eq!(parsed.resolvers[0].resolver.host, "1.1.1.1");
        assert_eq!(parsed.resolvers[0].mode, ResolverMode::Recursive);
        assert_eq!(parsed.resolvers[1].resolver.host, "2.2.2.2");
        assert_eq!(parsed.resolvers[1].resolver.port, 5353);
        assert_eq!(parsed.resolvers[2].resolver.host, "3.3.3.3");
        assert_eq!(parsed.resolvers[2].mode, ResolverMode::Authoritative);
        assert_eq!(parsed.resolvers[3].resolver.host, "4.4.4.4");
        assert!(!parsed.authoritative_remote);
    }

    #[test]
    fn plugin_domain_single_entry() {
        let options = vec![sip003::Sip003Option {
            key: "domain".to_string(),
            value: "example.com".to_string(),
        }];
        let domain = parse_domain_option(&options)
            .expect("options should parse")
            .expect("domain should exist");
        assert_eq!(domain, "example.com");
    }

    #[test]
    fn plugin_domain_rejects_repeated_option() {
        let options = vec![
            sip003::Sip003Option {
                key: "domain".to_string(),
                value: "example.com".to_string(),
            },
            sip003::Sip003Option {
                key: "domain".to_string(),
                value: "example.net".to_string(),
            },
        ];
        assert!(parse_domain_option(&options).is_err());
    }

    #[test]
    fn plugin_domain_rejects_multiple_entries() {
        let options = vec![sip003::Sip003Option {
            key: "domain".to_string(),
            value: "example.com,example.net".to_string(),
        }];
        assert!(parse_domain_option(&options).is_err());
    }

    #[test]
    fn authoritative_flag_applies_to_remote() {
        let options = vec![sip003::Sip003Option {
            key: "authoritative".to_string(),
            value: "".to_string(),
        }];
        let parsed = parse_resolvers_from_options(&options).expect("options should parse");
        assert!(parsed.resolvers.is_empty());
        assert!(parsed.authoritative_remote);
    }
}
