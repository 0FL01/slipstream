# DNS Tunnel

This is the stable Android contract for the DNS Tunnel profile, Rust Slipstream
sidecar and FlowRelay boundary. Historical implementation and acceptance evidence
lives in [`goals/`](goals/).

The MasterDnsVPN migration was rolled back by user request on 2026-09-07.
The active client retains Slipstream's aggregate 56-worker/64-queue budget, with
an explicit experimental Yandex multipath option described below.
Slipstream now explicitly selects `dcubic` congestion control. Six paired LTE tests
measured +27.3% download and +42.7% upload, with loaded p95 increasing from 0.907 s
to 1.084 s. The user explicitly accepted this latency trade-off for throughput;
it did not pass the original latency gate. See
[`goals/2026-09-07-slipstream-dcubic-ab.md`](goals/2026-09-07-slipstream-dcubic-ab.md).
Later modern-BBR3 and resolver-aware pacing candidates did not displace DCUBIC:
see [four-variant comparison and rejected candidates](goals/2026-09-07-bbr3-resolver-experiment.md).
The MasterDNS Base36 codec was measured but rejected for migration: active-domain
capacity gain is only 1.38% and mobile encoding is slower. See
[Base36 codec experiment](goals/2026-09-07-base36-experiment.md).
ARQ is already supplied by QUIC, including enabled preemptive repeats; an extra
reliability layer has no demonstrated LTE benefit. See
[ARQ reconnaissance and baseline test limitations](goals/2026-09-08-arq-recon.md).
The subsequent 48-to-56-worker change improved paired-median download by 24.48%
and upload by 23.85% across six fresh forced-DNS LTE pairs. Loaded p95 increased
from 0.819 to 0.847 s, with a 6.19 s maximum outlier; all required transfers passed.
Reducing the queue to 16 then lost throughput and was rejected. See
[throughput iteration and rollback](goals/2026-09-08-dns-throughput-iteration.md).
The subsequent strict whole-query deadline candidate passed 12 isolated tests but
was rejected after balanced phone screening: paired-median download -4.03%, upload
+4.90%, below the 10% target. The 56-worker/64-queue exchange implementation was
retained; deadline code is retained only as a test fixture. See
[deadline experiment and evidence](goals/2026-09-08-dns-query-deadline.md).
Retained `bin/lib/masterdns/` is experimental source, not part of the active build.
Comparison results and rollback evidence remain in
[`goals/2026-09-07-masterdns-migration.md`](goals/2026-09-07-masterdns-migration.md).

## Data path

```text
application TCP or UDP
  -> Owenclave gVisor VPN
  -> authenticated loopback SOCKS5
  -> Rust Slipstream sidecar
  -> one QUIC stream per application flow or UDP association
  -> server-side FlowRelay
  -> deployment-owned egress
```

DNS Tunnel is a dedicated `DnsttBean`/`TYPE_DNSTT` profile. Its app-owned canonical
provisioning credential is
`owenclave-dns://<domain>?token=<32-lowercase-hex>`. The scanner and Configuration
clipboard action accept it and update the domain and Flow token atomically; the
profile editor also exposes both values as separate fields. The domain is stored per
profile and normalized to a lowercase ASCII FQDN. Ports, paths, fragments, duplicate
or unknown query fields, IP literals, carrier-incompatible domains over 238 characters
and malformed tokens are rejected.

A standalone raw 32-lowercase-hex token remains compatible. A new clipboard import
uses the historical `t.x.ass-peak.de` domain; scanning a raw token while editing an
existing profile preserves that profile's domain. The resolver, profile name and
benchmark snapshot are deliberately excluded from the credential. This scheme is an
Owenclave provisioning contract, not the unrelated community `dnstt://` public-key
format. Generic SSH profiles, Android deep-link registration and DNS profile export
are intentionally absent.

The bundled Slipstream certificate remains the trust anchor for every profile.
Changing the domain therefore supports another delegated name for the compatible
pinned service, not an arbitrary server or certificate. Legacy stored profiles are
read with the historical domain. Changing a profile's domain or token clears its old
resolver benchmark snapshot.

Automatic mode stores no resolver. At startup Owenclave snapshots the active
non-VPN underlay and tries the first two unique DNS addresses over TCP, then TCP
`77.88.8.8:53` and `77.88.8.1:53`, with duplicates removed. Candidates run
sequentially until real Slipstream `Connection ready`; a failed child is stopped
before the next starts. Manual mode instead uses exactly one validated
`udp://host:port` or `tcp://host:port` with no fallback.

Manual `tcp+mp://77.88.8.88:53` explicitly opts into the fixed Yandex Safe
`77.88.8.88:53` / Basic `77.88.8.1:53` TCP pair. Other multipath hosts/ports are
rejected; ordinary `tcp://77.88.8.88:53` remains single. Both paths share one
native QUIC connection and one child, not two tunnels. This mixed pair does not
promise Safe filtering, independent provider capacity or a universal speed gain.
Automatic startup and the separate foreground resolver benchmark remain single-path.

Six fresh alternating CT/TC forced-DNS LTE pairs on the same corrected APK measured
paired-median download +17.09% and acknowledged upload +30.84%, each with 5/6 wins.
Loaded p95 was 0.707 -> 0.696 s, maximum 0.708 -> 0.727 s; required transfers all
passed. One upload window lost 40.24%, so the option remains experimental. Earlier
accepted-APK comparisons included a control upload timeout and are retained, not
counted as six valid upload pairs. See [multipath results, fixes and rollback](goals/2026-09-09-yandex-multipath.md).

Using UDP instead of TCP for the same resolver is a rejected performance path for the
tested LTE route. In a short production-gVisor A/B, UDP remained functional but exact
download throughput averaged 18.121 kB/s versus 197.070 kB/s over TCP, the acknowledged
upload did not complete and 4 KiB latency was 4.66x worse. The strict TCP selection was
restored. Do not repeat this transport-only experiment without materially new network
evidence; see
[`goals/2026-08-12-dns-tunnel-udp-resolver-ab.md`](goals/2026-08-12-dns-tunnel-udp-resolver-ab.md).

The explicit DNS benchmark keeps a separate labeled pool: the automatic candidates,
all Yandex Basic, Safe and Family IPv4 pairs, and the nine user-supplied Rostelecom
scan candidates. The latter are marked Experimental and are never part of automatic
startup. Candidates run sequentially while the VPN is stopped; each gets one bounded
512 KiB or three-second download through one temporary child. Results rank measured
throughput before latency without an automatic recommendation. Selecting a result
stores it as the existing strict manual override; choosing Automatic clears it.
The last fully completed result set and selected benchmark host are stored with that
profile. Reopening the benchmark shows this snapshot without starting a child; Retest
runs the same foreground benchmark and replaces the snapshot only after success, so
cancellation or failure keeps the previous one. Cancel keeps the dialog visible until
the active child has stopped, then makes connection controls available again. Invalid
saved snapshots are ignored. The first resolver or Automatic choice is accepted and
the selector is disabled while cleanup and persistence finish.
Profile batch testing is unavailable while the proxy service is starting or connected,
so it cannot create a second DNS Tunnel child beside the active VPN.
The first reachable Cloudflare, OVH or Hetzner download endpoint is fixed for the
whole benchmark so endpoint fallback does not multiply runs or make resolver scores
incomparable.

The Exclave outbound uses the authenticated loopback SOCKS boundary. Application
TCP and UDP are supported; each TCP flow or UDP association gets an independent
local connection and Slipstream QUIC stream. Mux, health-probe streams and a
non-DNS carrier fallback are not part of this contract. Multipath scheduling is
QUIC packet-level, not application-stream assignment to resolvers.

Global Route Mode does not override that boundary for DNS Tunnel. In particular,
`Direct` remains available for other profile types but cannot route DNS Tunnel or its
benchmark payload around the authenticated carrier.

## Rejected direct UDP upgrade

A restricted-LTE probe used the DNS Tunnel only for authenticated rendezvous and
retained one cellular UDP socket after successful STUN. DNS control succeeded, but
`n-de1` received no direct UDP and the client received neither server-first nor
simultaneous-punch probes. DNS bootstrap therefore did not open a direct `n-de1`
path under the tested carrier allowlist.

Do not build a direct `n-de1` QUIC or GOST path on this mechanism. Any future fast
transport must first prove that its endpoint is independently reachable through
the carrier allowlist; DNS remains the data path rather than a one-time bootstrap.

## Runtime constraints

FlowRelay source supports an explicit, disabled-by-default private test-token
dispatch for same-carrier egress attribution. All four flags are required together:
`-test-token-file`, `-test-backend-token-file`, `-test-backend` and `-test-interface`.
The fixed numeric private backend receives only authenticated test OPENs, rewritten
with a separate backend token; all three credentials must differ. Production tokens
retain the deployment's original interface-bound handling, unknown tokens fail
closed, and a failed test backend has no fallback. The16 test-session cap shares
the existing global limit. A direct test backend additionally uses `-deny-private`;
`-check` validates configuration without listening. Credentials are protected files,
not command-line values. This facility is not enabled on n-de1 after the campaign;
see [direct/WARP attribution and teardown](goals/2026-09-09-multipath-throughput.md).

Native optimization uses the foreground [Android Python smoke harness](../bin/lib/slipstream/smoke/README.md)
before candidate APK builds. Its acknowledged small-upload workload also runs through
the real APK/gVisor path; Python adapter results alone never authorize a release.
The HTTPS fixture and private direct test dispatch are ephemeral, not production services.

- DNS Tunnel supports gVisor TUN only. System TUN cannot protect sockets opened by
  the separate Slipstream process. Proxy service mode is also rejected because it
  creates no Android VPN and cannot capture application traffic through gVisor.
- The Flow token and ephemeral loopback credentials reach the child only through
  stdin, not argv, environment variables or temporary files.
- VPN startup allows 15 seconds for aggregate carrier readiness; latency tests
  allow 5 seconds and are serialized process-wide.
- Automatic candidate exhaustion, carrier failure and FlowRelay failure are
  fail-closed. When Android changes the physical underlay, Owenclave closes the old
  child and restarts DNS Tunnel; automatic mode discovers the new network's DNS,
  while a manually selected resolver stays pinned. A child exit after readiness
  restarts the full DNS Tunnel session through the same bounded readiness gate; if no
  resolver becomes ready, the service stops instead of remaining falsely Connected.
  A user or fatal stop received during restart teardown cancels that queued restart.
  There is no periodic health check, background ranking, direct carrier, legacy SSH
  path or direct destination fallback.
- Runtime retains one child and either one resolver or the explicit two-path pair.
  Multipath bootstrap tries each configured endpoint once, allowing 3 seconds per
  pre-readiness attempt inside the existing aggregate deadline. Both-dead and wrong
  certificate cases fail closed. A server forced-path identity/CID guard is required
  before enabling multipath; see `bin/lib/slipstream/picoquic-multipath.patch`.
  Busy uses the existing 50 ms
  pacing and 400 ms keepalive; Warm polls at most once per 400 ms; quiet open streams
  poll at most once per 2 seconds; empty connections do not explicitly poll. Quiet
  and empty keepalive remains 5 seconds.
- The single TCP DNS adapter uses 56 serial workers with a separate 64-query queue;
  multipath splits this into two 28-worker/32-query adapters with distinct loopback
  UDP ports. Responses keep their source path identity. Busy native poll allowance
  remains per path, so equal worker counts do not imply equal DNS query load or
  battery cost; no battery improvement is claimed.
  increasing workers must not implicitly grow the queue. Each persistent resolver
  connection has one outstanding exchange, with no pipelining. The earlier 48-worker
  clean-APK six-pair comparison measured +34.9% download, -6.6% upload and improved loaded
  p95 on the tested TCP resolver; this is path-specific, not a universal guarantee.
  See [performance experiments and acceptance](goals/2026-09-07-dns-tunnel-performance.md).
- A captured TCP flow whose carrier dial fails is closed and removed from the active
  connection set immediately; failed flows are not retained until VPN shutdown.
- The generated Android Slipstream artifact is currently arm64-only and ignored
  under `app/src/main/jniLibs/`. A clean build must run
  `./bin/lib/slipstream/build.sh`.

## Ownership

This repository owns profile persistence and setup, generated Exclave config,
Android sidecar lifecycle, pinned Slipstream patches, the FlowRelay wire contract,
and the focused `flowd` source/tests. The relevant boundaries are:

- `app/src/main/java/io/nekohasekai/sagernet/fmt/dnstt/DnsttFmt.kt`
- `app/src/main/java/io/nekohasekai/sagernet/fmt/dnstt/DnsttBean.java`
- `app/src/main/java/io/nekohasekai/sagernet/fmt/ConfigBuilder.kt`
- `app/src/main/java/io/nekohasekai/sagernet/bg/proto/SlipstreamInstance.kt`
- `app/src/main/java/io/nekohasekai/sagernet/bg/proto/V2RayInstance.kt`
- `bin/lib/slipstream/`

The Slipstream implementation is built from a pinned external upstream and runs
as an Android native sidecar. Public authoritative listeners, deployed `flowd`,
service units, firewall policy, resolver delegation and final egress are owned by
the upper operations repository and live server configuration. Although `flowd`
source stays here beside its wire tests, this repository is not the deployment
source of truth.

Do not commit or print tokens, complete profile links, private keys, keystores or
credential databases.
