package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func dispatchClient(t *testing.T, ctx context.Context, s *server) (*net.TCPConn, <-chan struct{}) {
	t.Helper()
	listener, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() {
		defer close(done)
		defer listener.Close()
		conn, err := listener.AcceptTCP()
		if err == nil {
			defer conn.Close()
			s.handle(ctx, conn)
		}
	}()
	conn, err := net.DialTCP("tcp", nil, listener.Addr().(*net.TCPAddr))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close(); listener.Close() })
	conn.SetDeadline(time.Now().Add(2 * time.Second))
	return conn, done
}

func TestDispatchAuthorization(t *testing.T) {
	production, testToken, backendToken := [16]byte{1}, [16]byte{2}, [16]byte{3}
	for _, command := range []byte{commandTCP, commandUDP} {
		for _, credential := range []string{"production", "test", "unknown", "backend", "disabled"} {
			t.Run(string(rune('0'+command))+"/"+credential, func(t *testing.T) {
				ctx, cancel := context.WithCancel(context.Background())
				defer cancel()
				var productionCalls, testCalls atomic.Int32
				backend, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
				if err != nil {
					t.Fatal(err)
				}
				defer backend.Close()
				backendResult := make(chan bool, 1)
				payload := bytes.Repeat([]byte("bounded independent payload"), 1024)
				go func() {
					conn, err := backend.AcceptTCP()
					if err != nil {
						return
					}
					defer conn.Close()
					conn.SetDeadline(time.Now().Add(time.Second))
					open, err := parseOpen(conn, backendToken, time.Second, nil)
					if err != nil {
						backendResult <- false
						return
					}
					body, err := io.ReadAll(io.LimitReader(conn, int64(len(payload)+1)))
					valid := err == nil && bytes.Equal(body, payload) && open.command == command &&
						open.destination == (destination{host: "example.com", port: 443})
					if valid {
						_, err = conn.Write(body)
					}
					backendResult <- valid && err == nil
				}()
				s := &server{
					config: config{headerTimeout: time.Second, connectTimeout: time.Second}, token: production,
					dial: func(context.Context, destination) (*net.TCPConn, error) {
						productionCalls.Add(1)
						return nil, context.Canceled
					},
					listenUDP: func(context.Context) (*net.UDPConn, error) {
						productionCalls.Add(1)
						return nil, context.Canceled
					},
					test: &testDispatch{token: testToken, backendToken: backendToken, limit: make(chan struct{}, 8),
						dial: func(context.Context) (*net.TCPConn, error) {
							testCalls.Add(1)
							return net.DialTCP("tcp", nil, backend.Addr().(*net.TCPAddr))
						}},
				}
				token := testToken
				switch credential {
				case "production":
					token = production
				case "unknown":
					token = [16]byte{4}
				case "backend":
					token = backendToken
				case "disabled":
					s.test = nil
				}
				client, done := dispatchClient(t, ctx, s)
				header := append([]byte{flowVersion<<5 | command<<3 | addressDomain<<1}, token[:]...)
				header = append(header, 11)
				header = append(header, "example.com"...)
				header = binary.BigEndian.AppendUint16(header, 443)
				client.Write(header)
				if credential == "test" {
					client.Write(payload)
				}
				client.CloseWrite()
				body, _ := io.ReadAll(client)
				select {
				case <-done:
				case <-time.After(time.Second):
					t.Fatal("handler leaked")
				}
				if credential == "test" {
					if !bytes.Equal(body, payload) || !<-backendResult || testCalls.Load() != 1 || productionCalls.Load() != 0 {
						t.Fatal("test auth/header/payload/half-close isolation failed")
					}
				} else if len(body) != 0 || testCalls.Load() != 0 ||
					(credential == "production" && productionCalls.Load() != 1) ||
					(credential != "production" && productionCalls.Load() != 0) {
					t.Fatal("wrong routing or credential leakage")
				}
			})
		}
	}
}

func TestDispatchFailureDeadlinesAndLimits(t *testing.T) {
	for _, mode := range []string{"unavailable", "dial-timeout", "slow-header", "limit", "cancel-half-closed"} {
		t.Run(mode, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			var productionCalls, testCalls atomic.Int32
			connected := make(chan struct{})
			s := &server{config: config{headerTimeout: 50 * time.Millisecond, connectTimeout: 50 * time.Millisecond},
				token: [16]byte{1},
				dial: func(context.Context, destination) (*net.TCPConn, error) {
					productionCalls.Add(1)
					return nil, context.Canceled
				},
				test: &testDispatch{token: [16]byte{2}, backendToken: [16]byte{3}, limit: make(chan struct{}, 1)},
			}
			s.test.dial = func(ctx context.Context) (*net.TCPConn, error) {
				testCalls.Add(1)
				if mode == "dial-timeout" {
					<-ctx.Done()
				}
				return nil, context.DeadlineExceeded
			}
			if mode == "limit" {
				s.test.limit <- struct{}{}
			}
			if mode == "cancel-half-closed" {
				backend, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
				if err != nil {
					t.Fatal(err)
				}
				defer backend.Close()
				go func() {
					conn, err := backend.AcceptTCP()
					if err == nil {
						defer conn.Close()
						io.Copy(io.Discard, conn)
						close(connected)
						<-ctx.Done()
					}
				}()
				s.test.dial = func(context.Context) (*net.TCPConn, error) {
					testCalls.Add(1)
					return net.DialTCP("tcp", nil, backend.Addr().(*net.TCPAddr))
				}
			}
			client, done := dispatchClient(t, ctx, s)
			header := append([]byte{0x20}, s.test.token[:]...)
			if mode != "slow-header" {
				header = append(header, 1, 1, 1, 1, 0, 80)
			}
			client.Write(header)
			if mode == "cancel-half-closed" {
				client.CloseWrite()
				select {
				case <-connected:
				case <-time.After(time.Second):
					t.Fatal("backend did not receive EOF")
				}
				cancel()
			}
			select {
			case <-done:
			case <-time.After(time.Second):
				t.Fatal("deadline or cancellation failed")
			}
			if productionCalls.Load() != 0 || ((mode == "limit" || mode == "slow-header") && testCalls.Load() != 0) {
				t.Fatal("failure fell through or exceeded test limit")
			}
		})
	}
}

func TestDispatchHeaderBound(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()
	token := [16]byte{2}
	name := strings.Repeat("a", 63) + "." + strings.Repeat("b", 63) + "." + strings.Repeat("c", 63) + "." + strings.Repeat("d", 61)
	header := append([]byte{0x24}, token[:]...)
	header = append(header, byte(len(name)))
	header = append(header, name...)
	header = append(header, 0, 80)
	go right.Write(header)
	open, err := parseOpen(left, [16]byte{1}, time.Second, &token)
	if err != nil || !bytes.Equal(open.testHeader, header) || len(open.testHeader) != 273 || cap(open.testHeader) > 512 {
		t.Fatal("test header is not bounded")
	}
}

func TestDirectDestinationDenial(t *testing.T) {
	_, resolve, _, err := newWarpNetwork("lo", "1.1.1.1:53", true)
	if err != nil {
		t.Fatal(err)
	}
	for _, address := range []string{"127.0.0.1", "10.200.0.2", "172.16.1.1", "192.168.1.1", "169.254.1.1", "100.64.1.1", "0.0.0.0", "224.1.1.1", "255.255.255.255", "::1", "fc00::1", "fe80::1", "ff02::1", "::ffff:10.200.0.2"} {
		if _, err := resolve(context.Background(), destination{host: address, port: 443}); err == nil {
			t.Fatalf("accepted non-public address %s", address)
		}
	}
	if _, err := resolve(context.Background(), destination{host: "1.1.1.1", port: 443}); err != nil {
		t.Fatal(err)
	}
	_, productionResolve, _, _ := newWarpNetwork("lo", "1.1.1.1:53", false)
	if _, err := productionResolve(context.Background(), destination{host: "10.200.0.2", port: 443}); err != nil {
		t.Fatal("disabled option changed production resolution")
	}
}

func TestDispatchConfiguration(t *testing.T) {
	base := []string{"-token-file", "/unused"}
	for _, flags := range [][]string{
		{"-test-token-file", "/unused"},
		{"-test-token-file", "/unused", "-test-backend-token-file", "/unused", "-test-interface", "lo", "-test-backend", "8.8.8.8:40003"},
		{"-test-token-file", "/unused", "-test-backend-token-file", "/unused", "-test-interface", "lo", "-test-backend", "10.200.0.2:40001"},
		{"-test-token-file", "/unused", "-test-backend-token-file", "/unused", "-test-interface", "lo", "-test-backend", "127.0.0.1:abc"},
	} {
		if _, err := parseConfig(append(base, flags...)); err == nil {
			t.Fatal("unsafe config accepted")
		}
	}
	production := [16]byte{1}
	if dispatch, err := loadTestDispatch(config{}, production); err != nil || dispatch != nil {
		t.Fatal("not disabled by default")
	}
	for _, pair := range [][2]byte{{1, 3}, {2, 1}, {2, 2}, {2, 3}} {
		cfg := config{testTokenFile: filepath.Join(t.TempDir(), "test"), backendTokenFile: filepath.Join(t.TempDir(), "backend"), testDevice: "lo"}
		a, b := [16]byte{pair[0]}, [16]byte{pair[1]}
		if err := os.WriteFile(cfg.testTokenFile, a[:], 0600); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(cfg.backendTokenFile, b[:], 0600); err != nil {
			t.Fatal(err)
		}
		dispatch, err := loadTestDispatch(cfg, production)
		if (err == nil) != (pair == [2]byte{2, 3}) {
			t.Fatal("credential separation failed")
		}
		if err == nil && cap(dispatch.limit) != 16 {
			t.Fatal("test session limit changed")
		}
	}
}
