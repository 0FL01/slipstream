package config

import (
	"fmt"
	"strings"
	"testing"

	core "github.com/exclavenetwork/exclave-core/v5"
	"github.com/exclavenetwork/exclave-core/v5/common/serial"
	"github.com/exclavenetwork/exclave-core/v5/proxy/socks"
	"github.com/exclavenetwork/exclave-core/v5/transport/internet"
	"github.com/exclavenetwork/exclave-core/v5/transport/internet/tagged"
)

const retainedConfig = `{
  "log": {"access": "none", "error": "none", "loglevel": "none"},
  "dns": {
    "servers": [{
      "address": "1.1.1.1",
      "port": 53,
      "domains": ["domain:example.com"],
      "fallbackStrategy": "disabledIfAnyMatch"
    }],
    "hosts": {"carrier.example": "127.0.0.1"},
    "queryStrategy": "UseIPv4",
    "fallbackStrategy": "disabledIfAnyMatch"
  },
  "policy": {
    "levels": {"1": {"connIdle": 30, "statsUserUplink": true, "bufferSize": 4}},
    "system": {"statsOutboundUplink": true, "statsOutboundDownlink": true}
  },
  "routing": {
    "domainStrategy": "IPIfNonMatch",
    "domainMatcher": "mph",
    "rules": [{
      "type": "field",
      "domain": ["domain:example.com"],
      "ip": ["192.0.2.0/24", "2001:db8::/32"],
      "port": "53,80-81",
      "network": "tcp,udp",
      "source": ["198.51.100.0/24"],
      "sourcePort": "1024-65535",
      "user": ["carrier"],
      "inboundTag": ["socks-in"],
      "protocol": ["tls"],
      "attrs": "attrs[':method'] == 'GET'",
      "uid": [1000],
      "ssid": ["test-network"],
      "networkType": ["wifi"],
      "outboundTag": "carrier"
    }]
  },
  "stats": {},
  "inbounds": [
    {
      "tag": "ipc-in",
      "listen": "/tmp/owenclave-ipc.sock",
      "protocol": "ipc",
      "settings": {"level": 1},
      "sniffing": {"enabled": true, "destOverride": ["http", "tls", "quic"], "routeOnly": true},
      "dumpUID": true
    },
    {
      "tag": "socks-in",
      "listen": "127.0.0.1",
      "port": 1080,
      "protocol": "socks",
      "settings": {
        "auth": "password",
        "accounts": [{"user": "fixture-user", "pass": "fixture-pass"}],
        "udp": true,
        "ip": "127.0.0.1",
        "userLevel": 1,
        "deferLastReply": true
      }
    },
    {
      "tag": "benchmark",
      "listen": "127.0.0.1",
      "port": 1081,
      "protocol": "http",
      "settings": {
        "accounts": [{"user": "fixture-user", "pass": "fixture-pass"}],
        "allowTransparent": true,
        "userLevel": 1
      }
    },
    {
      "tag": "dns-unix-in",
      "listen": "/tmp/owenclave-dns.sock",
      "protocol": "dokodemo-door",
      "settings": {"address": "/ipc_dns.sock", "network": "unix"}
    },
    {
      "tag": "dns-in",
      "listen": "127.0.0.1",
      "port": 1053,
      "protocol": "dokodemo-door",
      "settings": {"address": "127.0.0.1", "port": 53, "network": "tcp,udp", "userLevel": 1}
    },
    {
      "tag": "redirect-in",
      "listen": "127.0.0.1",
      "port": 1082,
      "protocol": "dokodemo-door",
      "settings": {"network": "tcp", "followRedirect": true}
    }
  ],
  "outbounds": [
    {
      "tag": "carrier",
      "protocol": "socks",
      "domainStrategy": "UseIPv4",
      "dialDomainStrategy": "AsIs",
      "settings": {
        "servers": [{
          "address": "127.0.0.1",
          "port": 2080,
          "users": [{"user": "fixture-user", "pass": "fixture-pass"}]
        }],
        "version": "5",
        "delayAuthWrite": true,
        "uot": %t
      }
    },
    {
      "tag": "direct",
      "protocol": "freedom",
      "sendThrough": "127.0.0.1",
      "settings": {
        "domainStrategy": "PreferIPv4",
        "redirect": "127.0.0.1:8080",
        "userLevel": 1,
        "interruptConnections": true
      }
    },
    {
      "tag": "block",
      "protocol": "blackhole",
      "proxySettings": {"tag": "direct", "transportLayer": true},
      "settings": {"response": {"type": "http"}}
    },
    {
      "tag": "dns-out",
      "protocol": "dns",
      "settings": {
        "network": "udp",
        "address": "1.1.1.1",
        "port": 53,
        "userLevel": 1,
        "overrideResponseTTL": true,
        "responseTTL": 60,
        "nonIPQuery": "skip",
        "lookupAsExchange": true
      }
    }
  ]
}`

func TestRetainedConfigsInstantiate(t *testing.T) {
	if tagged.Dialer == nil {
		t.Fatal("tagged transport-layer proxy dialer is unregistered")
	}
	for _, protocol := range []string{"tcp", "udp"} {
		if _, err := internet.CreateTransportConfig(protocol); err != nil {
			t.Fatalf("%s transport is unregistered: %v", protocol, err)
		}
	}
	for _, test := range []struct {
		name string
		uot  bool
	}{
		{name: "olcrtc", uot: false},
		{name: "dnstt", uot: true},
	} {
		t.Run(test.name, func(t *testing.T) {
			built, err := LoadJSONConfig(strings.NewReader(fmt.Sprintf(retainedConfig, test.uot)))
			if err != nil {
				t.Fatal(err)
			}
			carrier, err := serial.GetInstanceOf(built.Outbound[0].ProxySettings)
			if err != nil {
				t.Fatal(err)
			}
			if got := carrier.(*socks.ClientConfig).Uot; got != test.uot {
				t.Fatalf("SOCKS UoT = %t, want %t", got, test.uot)
			}
			instance, err := core.New(built)
			if err != nil {
				t.Fatal(err)
			}
			if err := instance.Close(); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestRemovedConfigSurfaceIsRejected(t *testing.T) {
	for _, test := range []struct {
		name   string
		config string
	}{
		{name: "legacy protocol", config: `{"outbounds":[{"protocol":"vmess","settings":{}}]}`},
		{name: "stream settings", config: `{"outbounds":[{"protocol":"freedom","streamSettings":{}}]}`},
		{name: "mux", config: `{"outbounds":[{"protocol":"freedom","mux":{}}]}`},
		{name: "smux", config: `{"outbounds":[{"protocol":"freedom","smux":{}}]}`},
		{name: "balancer", config: `{"routing":{"balancers":[]}}`},
		{name: "unknown root", config: `{"observatory":{}}`},
	} {
		t.Run(test.name, func(t *testing.T) {
			if _, err := LoadJSONConfig(strings.NewReader(test.config)); err == nil {
				t.Fatal("config unexpectedly accepted")
			}
		})
	}
}
