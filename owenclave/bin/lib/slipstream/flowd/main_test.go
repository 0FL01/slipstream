package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"io"
	"net"
	"os"
	"path/filepath"
	"sync/atomic"
	"testing"
	"time"
)

func TestParseOpen(t *testing.T) {
	var token [16]byte
	copy(token[:], []byte("0123456789abcdef"))
	tests := []struct {
		name    string
		meta    byte
		addr    []byte
		port    uint16
		host    string
		command byte
		valid   bool
		token   [16]byte
	}{
		{name: "ipv4", meta: 0x20, addr: []byte{192, 0, 2, 1}, port: 443, host: "192.0.2.1", command: commandTCP, valid: true, token: token},
		{name: "ipv6", meta: 0x22, addr: net.ParseIP("2001:db8::1").To16(), port: 53, host: "2001:db8::1", command: commandTCP, valid: true, token: token},
		{name: "domain", meta: 0x24, addr: append([]byte{11}, []byte("example.com")...), port: 80, host: "example.com", command: commandTCP, valid: true, token: token},
		{name: "udp", meta: 0x28, addr: []byte{192, 0, 2, 1}, port: 53, host: "192.0.2.1", command: commandUDP, valid: true, token: token},
		{name: "wrong version", meta: 0x40, addr: []byte{192, 0, 2, 1}, port: 443, token: token},
		{name: "unsupported command", meta: 0x30, addr: []byte{192, 0, 2, 1}, port: 443, token: token},
		{name: "reserved bit", meta: 0x21, addr: []byte{192, 0, 2, 1}, port: 443, token: token},
		{name: "bad domain", meta: 0x24, addr: append([]byte{4}, []byte("a..b")...), port: 80, token: token},
		{name: "zero port", meta: 0x20, addr: []byte{192, 0, 2, 1}, token: token},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			left, right := net.Pipe()
			defer left.Close()
			defer right.Close()
			go func() {
				message := append([]byte{test.meta}, test.token[:]...)
				message = append(message, test.addr...)
				message = binary.BigEndian.AppendUint16(message, test.port)
				right.Write(message)
			}()
			target, err := parseOpen(left, token, time.Second, nil)
			if (err == nil) != test.valid {
				t.Fatalf("valid=%v err=%v", test.valid, err)
			}
			if test.valid && (target.command != test.command ||
				target.destination.host != test.host || target.destination.port != test.port) {
				t.Fatalf("target=%+v", target)
			}
		})
	}
}

func TestWrongTokenStopsBeforeDestination(t *testing.T) {
	var expected [16]byte
	var wrong [16]byte
	wrong[0] = 1
	left, right := net.Pipe()
	defer left.Close()
	go func() {
		defer right.Close()
		right.Write(append([]byte{0x20}, wrong[:]...))
	}()
	if _, err := parseOpen(left, expected, time.Second, nil); err == nil {
		t.Fatal("wrong token accepted")
	}
}

func TestWrongTokenDoesNotDial(t *testing.T) {
	listener, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.ParseIP("127.0.0.1")})
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	var token [16]byte
	var dialed atomic.Int32
	s := &server{
		config: config{headerTimeout: time.Second, connectTimeout: time.Second},
		token:  token,
		dial: func(context.Context, destination) (*net.TCPConn, error) {
			dialed.Add(1)
			return nil, context.Canceled
		},
	}
	done := make(chan struct{})
	go func() {
		conn, acceptErr := listener.AcceptTCP()
		if acceptErr == nil {
			s.handle(context.Background(), conn)
			conn.Close()
		}
		close(done)
	}()
	client, err := net.DialTCP("tcp", nil, listener.Addr().(*net.TCPAddr))
	if err != nil {
		t.Fatal(err)
	}
	wrong := token
	wrong[0] = 1
	client.Write(append([]byte{0x20}, wrong[:]...))
	client.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("handler did not finish")
	}
	if dialed.Load() != 0 {
		t.Fatal("wrong token reached dialer")
	}
}

func TestHeaderAndPayloadRelay(t *testing.T) {
	backend, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.ParseIP("127.0.0.1")})
	if err != nil {
		t.Fatal(err)
	}
	defer backend.Close()
	backendDone := make(chan error, 1)
	go func() {
		conn, err := backend.AcceptTCP()
		if err != nil {
			backendDone <- err
			return
		}
		defer conn.Close()
		_, err = io.Copy(conn, conn)
		backendDone <- err
	}()

	listener, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.ParseIP("127.0.0.1")})
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	var token [16]byte
	copy(token[:], []byte("0123456789abcdef"))
	dialed := atomic.Int32{}
	s := &server{
		config: config{headerTimeout: time.Second, connectTimeout: time.Second},
		token:  token,
		dial: func(ctx context.Context, target destination) (*net.TCPConn, error) {
			dialed.Add(1)
			conn, err := (&net.Dialer{}).DialContext(ctx, "tcp", backend.Addr().String())
			if err != nil {
				return nil, err
			}
			return conn.(*net.TCPConn), nil
		},
	}
	handlerDone := make(chan struct{})
	go func() {
		conn, acceptErr := listener.AcceptTCP()
		if acceptErr == nil {
			s.handle(context.Background(), conn)
			conn.Close()
		}
		close(handlerDone)
	}()

	client, err := net.DialTCP("tcp", nil, listener.Addr().(*net.TCPAddr))
	if err != nil {
		t.Fatal(err)
	}
	payload := bytes.Repeat([]byte("flow"), 1024)
	message := append([]byte{0x20}, token[:]...)
	message = append(message, []byte{127, 0, 0, 1}...)
	message = binary.BigEndian.AppendUint16(message, 443)
	message = append(message, payload...)
	if _, err := client.Write(message); err != nil {
		t.Fatal(err)
	}
	if err := client.CloseWrite(); err != nil {
		t.Fatal(err)
	}
	response, err := io.ReadAll(client)
	client.Close()
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(response, payload) {
		t.Fatalf("payload mismatch: got %d bytes", len(response))
	}
	if dialed.Load() != 1 {
		t.Fatalf("dialed %d times", dialed.Load())
	}
	select {
	case <-handlerDone:
	case <-time.After(time.Second):
		t.Fatal("handler did not finish")
	}
	if err := <-backendDone; err != nil {
		t.Fatal(err)
	}
}

func TestUDPRelay(t *testing.T) {
	backend, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.ParseIP("127.0.0.1")})
	if err != nil {
		t.Fatal(err)
	}
	defer backend.Close()
	backendDone := make(chan error, 1)
	go func() {
		buffer := make([]byte, 64)
		length, peer, err := backend.ReadFromUDP(buffer)
		if err == nil {
			_, err = backend.WriteToUDP(buffer[:length], peer)
		}
		backendDone <- err
	}()

	listener, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.ParseIP("127.0.0.1")})
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	var token [16]byte
	copy(token[:], []byte("0123456789abcdef"))
	s := &server{
		config: config{headerTimeout: time.Second, connectTimeout: time.Second},
		token:  token,
		resolve: func(context.Context, destination) ([]net.IPAddr, error) {
			return []net.IPAddr{{IP: backend.LocalAddr().(*net.UDPAddr).IP}}, nil
		},
		listenUDP: func(context.Context) (*net.UDPConn, error) {
			return net.ListenUDP("udp4", &net.UDPAddr{IP: net.ParseIP("127.0.0.1")})
		},
	}
	handlerDone := make(chan struct{})
	go func() {
		conn, acceptErr := listener.AcceptTCP()
		if acceptErr == nil {
			s.handle(context.Background(), conn)
			conn.Close()
		}
		close(handlerDone)
	}()

	client, err := net.DialTCP("tcp", nil, listener.Addr().(*net.TCPAddr))
	if err != nil {
		t.Fatal(err)
	}
	backendPort := uint16(backend.LocalAddr().(*net.UDPAddr).Port)
	nonce := []byte("flowrelay-udp-nonce")
	message := append([]byte{0x28}, token[:]...)
	message = append(message, []byte{127, 0, 0, 1}...)
	message = binary.BigEndian.AppendUint16(message, backendPort)
	message = append(message, addressIPv4, 127, 0, 0, 1)
	message = binary.BigEndian.AppendUint16(message, backendPort)
	message = binary.BigEndian.AppendUint16(message, uint16(len(nonce)))
	message = append(message, nonce...)
	if _, err := client.Write(message); err != nil {
		t.Fatal(err)
	}
	if err := client.SetReadDeadline(time.Now().Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	source, response, err := readUDPRecord(client)
	if err != nil {
		t.Fatal(err)
	}
	if source.host != "127.0.0.1" || source.port != backendPort || !bytes.Equal(response, nonce) {
		t.Fatalf("source=%+v response=%q", source, response)
	}
	client.Close()
	select {
	case <-handlerDone:
	case <-time.After(time.Second):
		t.Fatal("handler did not finish")
	}
	if err := <-backendDone; err != nil {
		t.Fatal(err)
	}
}

func TestWrongTokenUDPDoesNotOpenSocket(t *testing.T) {
	listener, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.ParseIP("127.0.0.1")})
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	var opened atomic.Int32
	s := &server{
		config: config{headerTimeout: time.Second, connectTimeout: time.Second},
		listenUDP: func(context.Context) (*net.UDPConn, error) {
			opened.Add(1)
			return nil, context.Canceled
		},
	}
	done := make(chan struct{})
	go func() {
		conn, acceptErr := listener.AcceptTCP()
		if acceptErr == nil {
			s.handle(context.Background(), conn)
			conn.Close()
		}
		close(done)
	}()
	client, err := net.DialTCP("tcp", nil, listener.Addr().(*net.TCPAddr))
	if err != nil {
		t.Fatal(err)
	}
	wrong := [16]byte{1}
	client.Write(append([]byte{0x28}, wrong[:]...))
	client.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("handler did not finish")
	}
	if opened.Load() != 0 {
		t.Fatal("wrong token opened UDP socket")
	}
}

func TestReadTokenExactLength(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "token")
	if err := os.WriteFile(path, make([]byte, 16), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := readToken(path); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, make([]byte, 17), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := readToken(path); err == nil {
		t.Fatal("long token accepted")
	}
}

func TestParseConfigRejectsPublicListener(t *testing.T) {
	if _, err := parseConfig([]string{"-token-file", "/x", "-listen", "0.0.0.0:40001"}); err == nil {
		t.Fatal("public listener accepted")
	}
}
