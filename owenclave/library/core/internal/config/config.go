// Package config parses only the config surface emitted for retained carrier profiles.
package config

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"path/filepath"
	"strings"

	"github.com/golang/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"

	core "github.com/exclavenetwork/exclave-core/v5"
	"github.com/exclavenetwork/exclave-core/v5/app/dispatcher"
	"github.com/exclavenetwork/exclave-core/v5/app/policy"
	"github.com/exclavenetwork/exclave-core/v5/app/proxyman"
	"github.com/exclavenetwork/exclave-core/v5/app/router"
	"github.com/exclavenetwork/exclave-core/v5/app/stats"
	vnet "github.com/exclavenetwork/exclave-core/v5/common/net"
	"github.com/exclavenetwork/exclave-core/v5/common/platform"
	"github.com/exclavenetwork/exclave-core/v5/common/protocol"
	"github.com/exclavenetwork/exclave-core/v5/common/serial"
	"github.com/exclavenetwork/exclave-core/v5/infra/conf/cfgcommon"
	"github.com/exclavenetwork/exclave-core/v5/infra/conf/cfgcommon/proxycfg"
	"github.com/exclavenetwork/exclave-core/v5/infra/conf/cfgcommon/sniffer"
	"github.com/exclavenetwork/exclave-core/v5/infra/conf/geodata"
	routeconf "github.com/exclavenetwork/exclave-core/v5/infra/conf/rule"
	syntheticdns "github.com/exclavenetwork/exclave-core/v5/infra/conf/synthetic/dns"
	syntheticlog "github.com/exclavenetwork/exclave-core/v5/infra/conf/synthetic/log"
	"github.com/exclavenetwork/exclave-core/v5/proxy/blackhole"
	dnsproxy "github.com/exclavenetwork/exclave-core/v5/proxy/dns"
	"github.com/exclavenetwork/exclave-core/v5/proxy/dokodemo"
	"github.com/exclavenetwork/exclave-core/v5/proxy/freedom"
	httpproxy "github.com/exclavenetwork/exclave-core/v5/proxy/http"
	"github.com/exclavenetwork/exclave-core/v5/proxy/ipc"
	"github.com/exclavenetwork/exclave-core/v5/proxy/socks"
)

type config struct {
	Log       *syntheticlog.LogConfig `json:"log"`
	DNS       *syntheticdns.DNSConfig `json:"dns"`
	Routing   *routingConfig          `json:"routing"`
	Policy    *policyConfig           `json:"policy"`
	Stats     *struct{}               `json:"stats"`
	Inbounds  []inboundConfig         `json:"inbounds"`
	Outbounds []outboundConfig        `json:"outbounds"`
}

type routingConfig struct {
	Rules          []json.RawMessage `json:"rules"`
	DomainStrategy string            `json:"domainStrategy"`
	DomainMatcher  string            `json:"domainMatcher"`
}

type policyConfig struct {
	Levels map[uint32]*levelPolicy `json:"levels"`
	System *systemPolicy           `json:"system"`
}

type levelPolicy struct {
	Handshake         *uint32 `json:"handshake"`
	ConnectionIdle    *uint32 `json:"connIdle"`
	UplinkOnly        *uint32 `json:"uplinkOnly"`
	DownlinkOnly      *uint32 `json:"downlinkOnly"`
	StatsUserUplink   bool    `json:"statsUserUplink"`
	StatsUserDownlink bool    `json:"statsUserDownlink"`
	BufferSize        *int32  `json:"bufferSize"`
}

type systemPolicy struct {
	StatsInboundUplink    bool `json:"statsInboundUplink"`
	StatsInboundDownlink  bool `json:"statsInboundDownlink"`
	StatsOutboundUplink   bool `json:"statsOutboundUplink"`
	StatsOutboundDownlink bool `json:"statsOutboundDownlink"`
	OverrideAccessLogDest bool `json:"overrideAccessLogDest"`
}

type inboundConfig struct {
	Protocol string                  `json:"protocol"`
	Port     *cfgcommon.PortRange    `json:"port"`
	Listen   *cfgcommon.Address      `json:"listen"`
	Settings json.RawMessage         `json:"settings"`
	Tag      string                  `json:"tag"`
	Sniffing *sniffer.SniffingConfig `json:"sniffing"`
	DumpUID  bool                    `json:"dumpUID"`
}

type outboundConfig struct {
	Protocol           string                `json:"protocol"`
	SendThrough        *cfgcommon.Address    `json:"sendThrough"`
	Tag                string                `json:"tag"`
	Settings           json.RawMessage       `json:"settings"`
	ProxySettings      *proxycfg.ProxyConfig `json:"proxySettings"`
	DomainStrategy     string                `json:"domainStrategy"`
	DialDomainStrategy string                `json:"dialDomainStrategy"`
	StreamSettings     json.RawMessage       `json:"streamSettings"`
	Mux                json.RawMessage       `json:"mux"`
	SMux               json.RawMessage       `json:"smux"`
}

type socksAccount struct {
	Username string `json:"user"`
	Password string `json:"pass"`
}

type socksInbound struct {
	AuthMethod     string             `json:"auth"`
	Accounts       []*socksAccount    `json:"accounts"`
	UDP            bool               `json:"udp"`
	Host           *cfgcommon.Address `json:"ip"`
	UserLevel      uint32             `json:"userLevel"`
	DeferLastReply bool               `json:"deferLastReply"`
}

type httpAccount struct {
	Username string            `json:"user"`
	Password string            `json:"pass"`
	Headers  map[string]string `json:"headers"`
}

type httpInbound struct {
	Accounts    []*httpAccount `json:"accounts"`
	Transparent bool           `json:"allowTransparent"`
	UserLevel   uint32         `json:"userLevel"`
}

type dokodemoInbound struct {
	Host      *cfgcommon.Address     `json:"address"`
	Port      uint16                 `json:"port"`
	Networks  *cfgcommon.NetworkList `json:"network"`
	Redirect  bool                   `json:"followRedirect"`
	UserLevel uint32                 `json:"userLevel"`
}

type ipcInbound struct {
	Level uint32 `json:"level"`
}

type remoteServer struct {
	Address *cfgcommon.Address `json:"address"`
	Port    uint16             `json:"port"`
	Users   []json.RawMessage  `json:"users"`
}

type socksOutbound struct {
	Servers        []*remoteServer `json:"servers"`
	Version        string          `json:"version"`
	DelayAuthWrite bool            `json:"delayAuthWrite"`
	UoT            bool            `json:"uot"`
}

type freedomOutbound struct {
	DomainStrategy       string `json:"domainStrategy"`
	Redirect             string `json:"redirect"`
	UserLevel            uint32 `json:"userLevel"`
	InterruptConnections bool   `json:"interruptConnections"`
}

type blackholeOutbound struct {
	Response json.RawMessage `json:"response"`
}

type dnsOutbound struct {
	Network             cfgcommon.Network  `json:"network"`
	Address             *cfgcommon.Address `json:"address"`
	Port                uint16             `json:"port"`
	UserLevel           uint32             `json:"userLevel"`
	OverrideResponseTTL bool               `json:"overrideResponseTTL"`
	ResponseTTL         uint32             `json:"responseTTL"`
	NonIPQuery          string             `json:"nonIPQuery"`
	LookupAsExchange    bool               `json:"lookupAsExchange"`
}

func LoadJSONConfig(reader io.Reader) (*core.Config, error) {
	decoder := json.NewDecoder(reader)
	decoder.DisallowUnknownFields()
	var raw config
	if err := decoder.Decode(&raw); err != nil {
		return nil, fmt.Errorf("failed to read retained config: %w", err)
	}
	if err := decoder.Decode(&struct{}{}); !errors.Is(err, io.EOF) {
		return nil, errors.New("retained config must contain one JSON object")
	}
	return raw.build()
}

func (c *config) build() (*core.Config, error) {
	result := &core.Config{App: []*anypb.Any{
		serial.ToTypedMessage(&dispatcher.Config{}),
		serial.ToTypedMessage(&proxyman.InboundConfig{}),
		serial.ToTypedMessage(&proxyman.OutboundConfig{}),
	}}
	if c.Stats != nil {
		result.App = append(result.App, serial.ToTypedMessage(&stats.Config{}))
	}
	logConfig := syntheticlog.DefaultLogConfig()
	if c.Log != nil {
		logConfig = c.Log.Build()
	}
	result.App = append([]*anypb.Any{serial.ToTypedMessage(logConfig)}, result.App...)
	if c.Routing != nil {
		built, err := c.Routing.build()
		if err != nil {
			return nil, fmt.Errorf("failed to parse routing config: %w", err)
		}
		result.App = append(result.App, serial.ToTypedMessage(built))
	}
	if c.DNS != nil {
		built, err := c.DNS.Build()
		if err != nil {
			return nil, fmt.Errorf("failed to parse DNS config: %w", err)
		}
		result.App = append(result.App, serial.ToTypedMessage(built))
	}
	if c.Policy != nil {
		result.App = append(result.App, serial.ToTypedMessage(c.Policy.build()))
	}
	for i := range c.Inbounds {
		built, err := c.Inbounds[i].build()
		if err != nil {
			return nil, fmt.Errorf("failed to parse inbound %q: %w", c.Inbounds[i].Tag, err)
		}
		result.Inbound = append(result.Inbound, built)
	}
	for i := range c.Outbounds {
		built, err := c.Outbounds[i].build()
		if err != nil {
			return nil, fmt.Errorf("failed to parse outbound %q: %w", c.Outbounds[i].Tag, err)
		}
		result.Outbound = append(result.Outbound, built)
	}
	return result, nil
}

func (c *policyConfig) build() *policy.Config {
	result := &policy.Config{Level: make(map[uint32]*policy.Policy, len(c.Levels))}
	for level, value := range c.Levels {
		if value == nil {
			continue
		}
		timeout := &policy.Policy_Timeout{}
		if value.Handshake != nil {
			timeout.Handshake = &policy.Second{Value: *value.Handshake}
		}
		if value.ConnectionIdle != nil {
			timeout.ConnectionIdle = &policy.Second{Value: *value.ConnectionIdle}
		}
		if value.UplinkOnly != nil {
			timeout.UplinkOnly = &policy.Second{Value: *value.UplinkOnly}
		}
		if value.DownlinkOnly != nil {
			timeout.DownlinkOnly = &policy.Second{Value: *value.DownlinkOnly}
		}
		built := &policy.Policy{Timeout: timeout, Stats: &policy.Policy_Stats{
			UserUplink: value.StatsUserUplink, UserDownlink: value.StatsUserDownlink,
		}}
		if value.BufferSize != nil {
			size := int32(-1)
			if *value.BufferSize >= 0 {
				size = *value.BufferSize * 1024
			}
			built.Buffer = &policy.Policy_Buffer{Connection: size}
		}
		result.Level[level] = built
	}
	if c.System != nil {
		result.System = &policy.SystemPolicy{
			Stats: &policy.SystemPolicy_Stats{
				InboundUplink: c.System.StatsInboundUplink, InboundDownlink: c.System.StatsInboundDownlink,
				OutboundUplink: c.System.StatsOutboundUplink, OutboundDownlink: c.System.StatsOutboundDownlink,
			},
			OverrideAccessLogDest: c.System.OverrideAccessLogDest,
		}
	}
	return result
}

func (c *routingConfig) build() (*router.Config, error) {
	built := &router.Config{}
	switch strings.ToLower(c.DomainStrategy) {
	case "alwaysip", "always_ip", "always-ip":
		built.DomainStrategy = router.DomainStrategy_UseIp
	case "ipifnonmatch", "ip_if_non_match", "ip-if-non-match":
		built.DomainStrategy = router.DomainStrategy_IpIfNonMatch
	case "ipondemand", "ip_on_demand", "ip-on-demand":
		built.DomainStrategy = router.DomainStrategy_IpOnDemand
	default:
		built.DomainStrategy = router.DomainStrategy_AsIs
	}
	ctx := cfgcommon.NewConfigureLoadingContext(context.Background())
	loaderName := platform.NewEnvFlag("exclave.conf.geoloader").GetValue(func() string {
		return "memconservative"
	})
	loader, err := geodata.GetGeoDataLoader(loaderName)
	if err != nil {
		return nil, fmt.Errorf("unable to create geo data loader: %w", err)
	}
	cfgcommon.SetGeoDataLoader(ctx, loader)
	for _, rawRule := range c.Rules {
		rule, err := routeconf.ParseRule(ctx, rawRule)
		if err != nil {
			return nil, err
		}
		if rule.DomainMatcher == "" {
			rule.DomainMatcher = c.DomainMatcher
		}
		built.Rule = append(built.Rule, rule)
	}
	return built, nil
}

func (c *inboundConfig) build() (*core.InboundHandlerConfig, error) {
	receiver := &proxyman.ReceiverConfig{}
	if c.Listen == nil {
		if c.Port == nil {
			return nil, errors.New("listener has neither listen address nor port")
		}
	} else {
		receiver.Listen = c.Listen.Build()
		address := c.Listen.Address
		isSocket := address.Family().IsDomain() && (filepath.IsAbs(address.Domain()) || strings.HasPrefix(address.Domain(), "@"))
		if c.Port == nil && !isSocket {
			return nil, errors.New("IP listener has no port")
		}
	}
	if c.Port != nil {
		receiver.PortRange = c.Port.Build()
	}
	if c.Sniffing != nil {
		built, err := c.Sniffing.Build()
		if err != nil {
			return nil, err
		}
		receiver.SniffingSettings = built
	}
	proxy, redirect, err := buildInboundProxy(c.Protocol, c.Settings)
	if err != nil {
		return nil, err
	}
	receiver.ReceiveOriginalDestination = redirect
	return &core.InboundHandlerConfig{
		Tag: c.Tag, ReceiverSettings: serial.ToTypedMessage(receiver),
		ProxySettings: serial.ToTypedMessage(proxy), DumpUid: c.DumpUID,
	}, nil
}

func buildInboundProxy(name string, raw json.RawMessage) (proto.Message, bool, error) {
	switch name {
	case "socks":
		var c socksInbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, false, err
		}
		built := &socks.ServerConfig{UdpEnabled: c.UDP, UserLevel: c.UserLevel, DeferLastReply: c.DeferLastReply}
		switch c.AuthMethod {
		case "", "noauth":
			built.AuthType = socks.AuthType_NO_AUTH
		case "password":
			built.AuthType = socks.AuthType_PASSWORD
		default:
			return nil, false, fmt.Errorf("unsupported SOCKS auth %q", c.AuthMethod)
		}
		if c.Host != nil {
			built.Address = c.Host.Build()
		}
		if len(c.Accounts) > 0 {
			built.Accounts = make(map[string]string, len(c.Accounts))
			for _, account := range c.Accounts {
				built.Accounts[account.Username] = account.Password
			}
		}
		return built, false, nil
	case "http":
		var c httpInbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, false, err
		}
		built := &httpproxy.ServerConfig{AllowTransparent: c.Transparent, UserLevel: c.UserLevel}
		if len(c.Accounts) > 0 {
			built.Accounts = make(map[string]string, len(c.Accounts))
			for _, account := range c.Accounts {
				built.Accounts[account.Username] = account.Password
			}
		}
		return built, false, nil
	case "dokodemo-door":
		var c dokodemoInbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, false, err
		}
		built := &dokodemo.Config{Port: uint32(c.Port), Networks: c.Networks.Build(), FollowRedirect: c.Redirect, UserLevel: c.UserLevel}
		if c.Host != nil {
			built.Address = c.Host.Build()
		}
		return built, c.Redirect, nil
	case "ipc":
		var c ipcInbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, false, err
		}
		return &ipc.ServerConfig{Level: int32(c.Level)}, false, nil
	default:
		return nil, false, fmt.Errorf("unsupported inbound protocol %q", name)
	}
}

func (c *outboundConfig) build() (*core.OutboundHandlerConfig, error) {
	if len(c.StreamSettings) != 0 && string(c.StreamSettings) != "null" {
		return nil, errors.New("streamSettings are unavailable in the carrier-only core")
	}
	if len(c.Mux) != 0 && string(c.Mux) != "null" {
		return nil, errors.New("mux is unavailable in the carrier-only core")
	}
	if len(c.SMux) != 0 && string(c.SMux) != "null" {
		return nil, errors.New("smux is unavailable in the carrier-only core")
	}
	sender := &proxyman.SenderConfig{}
	if c.SendThrough != nil {
		if c.SendThrough.Family().IsDomain() {
			return nil, errors.New("sendThrough must be an IP address")
		}
		sender.Via = c.SendThrough.Build()
	}
	if c.ProxySettings != nil {
		built, err := c.ProxySettings.Build()
		if err != nil {
			return nil, err
		}
		sender.ProxySettings = built
	}
	sender.DomainStrategy = parseDomainStrategy(c.DomainStrategy)
	if c.DialDomainStrategy == "" {
		sender.DialDomainStrategy = sender.DomainStrategy
	} else {
		sender.DialDomainStrategy = parseDomainStrategy(c.DialDomainStrategy)
	}
	proxy, err := buildOutboundProxy(c.Protocol, c.Settings)
	if err != nil {
		return nil, err
	}
	return &core.OutboundHandlerConfig{
		Tag: c.Tag, SenderSettings: serial.ToTypedMessage(sender), ProxySettings: serial.ToTypedMessage(proxy),
	}, nil
}

func buildOutboundProxy(name string, raw json.RawMessage) (proto.Message, error) {
	switch name {
	case "socks":
		var c socksOutbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, err
		}
		built := &socks.ClientConfig{Server: make([]*protocol.ServerEndpoint, len(c.Servers)), DelayAuthWrite: c.DelayAuthWrite, Uot: c.UoT}
		switch strings.ToLower(c.Version) {
		case "", "5":
			built.Version = socks.Version_SOCKS5
		default:
			return nil, fmt.Errorf("unsupported SOCKS version %q", c.Version)
		}
		for i, server := range c.Servers {
			if server.Address == nil {
				return nil, errors.New("SOCKS server address is missing")
			}
			endpoint := &protocol.ServerEndpoint{Address: server.Address.Build(), Port: uint32(server.Port)}
			for _, rawUser := range server.Users {
				var account socksAccount
				if err := decodeSettings(rawUser, &account); err != nil {
					return nil, err
				}
				endpoint.User = append(endpoint.User, &protocol.User{Account: serial.ToTypedMessage(&socks.Account{
					Username: account.Username, Password: account.Password,
				})})
			}
			built.Server[i] = endpoint
		}
		return built, nil
	case "freedom":
		var c freedomOutbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, err
		}
		built := &freedom.Config{DomainStrategy: parseFreedomStrategy(c.DomainStrategy), UserLevel: c.UserLevel, InterruptConnections: c.InterruptConnections}
		if c.Redirect != "" {
			host, portString, err := net.SplitHostPort(c.Redirect)
			if err != nil {
				return nil, fmt.Errorf("invalid freedom redirect: %w", err)
			}
			port, err := vnet.PortFromString(portString)
			if err != nil {
				return nil, fmt.Errorf("invalid freedom redirect port: %w", err)
			}
			built.DestinationOverride = &freedom.DestinationOverride{Server: &protocol.ServerEndpoint{
				Port: uint32(port),
			}}
			if host != "" {
				built.DestinationOverride.Server.Address = vnet.NewIPOrDomain(vnet.ParseAddress(host))
			}
		}
		return built, nil
	case "blackhole":
		var c blackholeOutbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, err
		}
		built := &blackhole.Config{}
		if len(c.Response) > 0 && string(c.Response) != "null" {
			var response struct {
				Type string `json:"type"`
			}
			if err := decodeSettings(c.Response, &response); err != nil {
				return nil, err
			}
			switch response.Type {
			case "none":
				built.Response = serial.ToTypedMessage(&blackhole.NoneResponse{})
			case "http":
				built.Response = serial.ToTypedMessage(&blackhole.HTTPResponse{})
			default:
				return nil, fmt.Errorf("unsupported blackhole response %q", response.Type)
			}
		}
		return built, nil
	case "dns":
		var c dnsOutbound
		if err := decodeSettings(raw, &c); err != nil {
			return nil, err
		}
		built := &dnsproxy.Config{Server: &vnet.Endpoint{Network: c.Network.Build(), Port: uint32(c.Port)}, UserLevel: c.UserLevel,
			OverrideResponseTtl: c.OverrideResponseTTL, ResponseTtl: c.ResponseTTL, Non_IPQuery: c.NonIPQuery, LookupAsExchange: c.LookupAsExchange}
		if c.Address != nil {
			built.Server.Address = c.Address.Build()
		}
		return built, nil
	default:
		return nil, fmt.Errorf("unsupported outbound protocol %q", name)
	}
}

func parseDomainStrategy(value string) proxyman.SenderConfig_DomainStrategy {
	switch strings.ToLower(value) {
	case "useip", "use_ip", "use-ip":
		return proxyman.SenderConfig_USE_IP
	case "useip4", "useipv4", "use_ip4", "use_ipv4", "use_ip_v4", "use-ip4", "use-ipv4", "use-ip-v4":
		return proxyman.SenderConfig_USE_IP4
	case "useip6", "useipv6", "use_ip6", "use_ipv6", "use_ip_v6", "use-ip6", "use-ipv6", "use-ip-v6":
		return proxyman.SenderConfig_USE_IP6
	case "preferip4", "preferipv4", "prefer_ip4", "prefer_ipv4", "prefer_ip_v4", "prefer-ip4", "prefer-ipv4", "prefer-ip-v4":
		return proxyman.SenderConfig_PREFER_IP4
	case "preferip6", "preferipv6", "prefer_ip6", "prefer_ipv6", "prefer_ip_v6", "prefer-ip6", "prefer-ipv6", "prefer-ip-v6":
		return proxyman.SenderConfig_PREFER_IP6
	default:
		return proxyman.SenderConfig_AS_IS
	}
}

func parseFreedomStrategy(value string) freedom.Config_DomainStrategy {
	return freedom.Config_DomainStrategy(parseDomainStrategy(value))
}

func decodeSettings(raw json.RawMessage, target any) error {
	if len(raw) == 0 || string(raw) == "null" {
		raw = []byte("{}")
	}
	decoder := json.NewDecoder(strings.NewReader(string(raw)))
	decoder.DisallowUnknownFields()
	return decoder.Decode(target)
}
