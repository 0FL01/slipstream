package config

import (
	_ "github.com/exclavenetwork/exclave-core/v5/app/dispatcher"
	_ "github.com/exclavenetwork/exclave-core/v5/app/dns"
	_ "github.com/exclavenetwork/exclave-core/v5/app/dns/fakedns"
	_ "github.com/exclavenetwork/exclave-core/v5/app/log"
	_ "github.com/exclavenetwork/exclave-core/v5/app/policy"
	_ "github.com/exclavenetwork/exclave-core/v5/app/proxyman/inbound"
	_ "github.com/exclavenetwork/exclave-core/v5/app/proxyman/outbound"
	_ "github.com/exclavenetwork/exclave-core/v5/app/router"
	_ "github.com/exclavenetwork/exclave-core/v5/app/stats"
	_ "github.com/exclavenetwork/exclave-core/v5/infra/conf/geodata/memconservative"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/blackhole"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/dns"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/dokodemo"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/freedom"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/http"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/ipc"
	_ "github.com/exclavenetwork/exclave-core/v5/proxy/socks"
	_ "github.com/exclavenetwork/exclave-core/v5/transport/internet/tagged/taggedimpl"
	_ "github.com/exclavenetwork/exclave-core/v5/transport/internet/tcp"
	_ "github.com/exclavenetwork/exclave-core/v5/transport/internet/udp"
)
