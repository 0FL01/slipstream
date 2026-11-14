package libexclavecore

import (
	"errors"
	"io"
	"net"
	"testing"

	v2rayNet "github.com/exclavenetwork/exclave-core/v5/common/net"
)

func TestNewConnectionClosesFailedDial(t *testing.T) {
	peer, incoming := net.Pipe()
	defer peer.Close()
	defer incoming.Close()

	tun := &Tun2ray{v2ray: &V2RayInstance{closed: true}}
	tun.NewConnection(
		v2rayNet.TCPDestination(v2rayNet.IPAddress(net.IPv4(192, 0, 2, 1)), 1234),
		v2rayNet.TCPDestination(v2rayNet.IPAddress(net.IPv4(198, 51, 100, 1)), 443),
		incoming,
	)

	if got := tun.connections.Len(); got != 0 {
		t.Fatalf("retained connections = %d, want 0", got)
	}
	if _, err := peer.Read(make([]byte, 1)); !errors.Is(err, io.EOF) && !errors.Is(err, io.ErrClosedPipe) {
		t.Fatalf("incoming connection remained open: %v", err)
	}
}
