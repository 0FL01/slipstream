module github.com/owenewans/libowenclavecore

go 1.26

require (
	filippo.io/age v1.3.1
	github.com/exclavenetwork/exclave-core/v5 v5.50.1-0.20260804210958-67d1a59820da
	github.com/exclavenetwork/go-stun v0.1.7-0.20260724055932-543999925444
	github.com/golang/protobuf v1.5.4
	github.com/quic-go/quic-go v0.61.0
	golang.org/x/mobile v0.0.0-20260803200217-62cee1672c8e
	golang.org/x/sys v0.47.0
	google.golang.org/protobuf v1.36.11
	gvisor.dev/gvisor v0.0.0-20250503011706-39ed1f5ac29c
)

require (
	filippo.io/hpke v0.4.0 // indirect
	filippo.io/mldsa v0.0.0-20260711112038-ff3f469cee29 // indirect
	github.com/adrg/xdg v0.5.3 // indirect
	github.com/andybalholm/brotli v1.0.6 // indirect
	github.com/google/btree v1.1.3 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	github.com/hashicorp/yamux v0.1.2 // indirect
	github.com/klauspost/compress v1.17.9 // indirect
	github.com/metacubex/utls v1.8.7 // indirect
	github.com/miekg/dns v1.1.72 // indirect
	github.com/pires/go-proxyproto v0.15.0 // indirect
	github.com/quic-go/qpack v0.6.0 // indirect
	github.com/refraction-networking/utls v1.8.3-0.20260623165621-880e27d8b0e5 // indirect
	github.com/sagernet/sing v0.8.13-0.20260804143108-f22b119cc7a2 // indirect
	github.com/sagernet/sing-mux v0.3.5 // indirect
	github.com/sagernet/smux v1.5.50-sing-box-mod.1 // indirect
	go4.org/netipx v0.0.0-20231129151722-fdeea329fbba // indirect
	golang.org/x/crypto v0.54.0 // indirect
	golang.org/x/exp v0.0.0-20250911091902-df9299821621 // indirect
	golang.org/x/mod v0.38.0 // indirect
	golang.org/x/net v0.57.0 // indirect
	golang.org/x/sync v0.22.0 // indirect
	golang.org/x/text v0.40.0 // indirect
	golang.org/x/time v0.7.0 // indirect
	golang.org/x/tools v0.48.0 // indirect
)

// workaround https://github.com/google/gvisor/commit/868dfbce4fd59f03145e2bc5ac0b585917c371fa
replace gvisor.dev/gvisor => gvisor.dev/gvisor v0.0.0-20250429202743-3a608a52255d
