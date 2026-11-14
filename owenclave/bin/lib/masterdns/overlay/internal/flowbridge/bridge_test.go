package flowbridge

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"io"
	"math/big"
	"net"
	"testing"
	"time"
)

func testCertificate(t *testing.T, expired bool) (tls.Certificate, []byte) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	end := time.Now().Add(time.Hour)
	if expired {
		end = time.Now().Add(-time.Hour)
	}
	template := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "test.invalid"},
		NotBefore: time.Now().Add(-2 * time.Hour), NotAfter: end, KeyUsage: x509.KeyUsageDigitalSignature}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	return tls.Certificate{Certificate: [][]byte{der}, PrivateKey: key}, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
}

func TestBootstrapBounds(t *testing.T) {
	for _, n := range []int{0, 79, 80, 81, 10000} {
		_, err := ReadCredentials(bytes.NewReader(make([]byte, n)))
		if (err == nil) != (n == 80) {
			t.Fatalf("unexpected bootstrap result for size %d", n)
		}
	}
}

func TestCertificatePin(t *testing.T) {
	for _, mode := range []string{"valid", "wrong-pin", "expired", "tls12"} {
		t.Run(mode, func(t *testing.T) {
			pair, cert := testCertificate(t, mode == "expired")
			if mode == "wrong-pin" {
				_, cert = testCertificate(t, false)
			}
			cfg, err := TLSConfig(cert, "test.invalid")
			if err != nil {
				t.Fatal(err)
			}
			left, right := net.Pipe()
			defer left.Close()
			defer right.Close()
			ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
			defer cancel()
			serverConfig := &tls.Config{Certificates: []tls.Certificate{pair}, NextProtos: []string{"owenclave-flowrelay/1"}}
			if mode == "tls12" {
				serverConfig.MaxVersion = tls.VersionTLS12
			}
			done := make(chan struct{})
			go func() { defer close(done); _ = tls.Server(right, serverConfig).HandshakeContext(ctx) }()
			err = tls.Client(left, cfg).HandshakeContext(ctx)
			if (err == nil) != (mode == "valid") {
				t.Fatal("unexpected certificate/protocol acceptance")
			}
			left.Close()
			right.Close()
			<-done
		})
	}
	if _, err := TLSConfig([]byte("not a certificate"), "test.invalid"); err == nil {
		t.Fatal("invalid trust accepted")
	}
}

func testSOCKS(t *testing.T, conn net.Conn, c Credentials, udp bool) {
	t.Helper()
	want := func(expected []byte) {
		b := make([]byte, len(expected))
		if _, err := io.ReadFull(conn, b); err != nil || !bytes.Equal(b, expected) {
			t.Fatal("SOCKS response mismatch")
		}
	}
	conn.Write([]byte{5, 1, 2})
	want([]byte{5, 2})
	auth := append([]byte{1, 32}, c.User[:]...)
	auth = append(auth, 32)
	auth = append(auth, c.Pass[:]...)
	conn.Write(auth)
	want([]byte{1, 0})
	request := []byte{5, 1, 0, 1, 192, 0, 2, 1, 1, 187}
	if udp {
		name := "sp.v2.udp-over-tcp.arpa"
		request = append([]byte{5, 1, 0, 3, byte(len(name))}, []byte(name)...)
		request = append(request, 0, 0)
	}
	conn.Write(request)
	want([]byte{5, 0, 0, 1, 0, 0, 0, 0, 0, 0})
	if udp {
		conn.Write([]byte{0, 1, 192, 0, 2, 1, 1, 187})
	}
}

func TestAuthenticatedTLSFlowRelay(t *testing.T) {
	for _, udp := range []bool{false, true} {
		t.Run(map[bool]string{false: "TCP", true: "UoT"}[udp], func(t *testing.T) {
			pair, cert := testCertificate(t, false)
			trust, err := TLSConfig(cert, "test.invalid")
			if err != nil {
				t.Fatal(err)
			}
			var credentials Credentials
			rand.Read(credentials.Token[:])
			rand.Read(credentials.User[:])
			rand.Read(credentials.Pass[:])
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			app, local := net.Pipe()
			defer app.Close()
			app.SetDeadline(time.Now().Add(3 * time.Second))
			raw, remote := net.Pipe()
			defer remote.Close()
			defer raw.Close()
			remote.SetDeadline(time.Now().Add(3 * time.Second))
			bridge := &Bridge{Credentials: credentials, TLS: trust, Dial: func(context.Context) (net.Conn, error) { return raw, nil }}
			done := make(chan struct{})
			go func() { defer close(done); bridge.Serve(ctx, local) }()
			payload := bytes.Repeat([]byte("synthetic-payload"), 256)
			serverResult := make(chan bool, 1)
			go func() {
				secure := tls.Server(remote, &tls.Config{Certificates: []tls.Certificate{pair}, MinVersion: tls.VersionTLS13, NextProtos: []string{"owenclave-flowrelay/1"}})
				open := make([]byte, 23)
				_, err := io.ReadFull(secure, open)
				meta := byte(0x20)
				if udp {
					meta = 0x28
				}
				valid := err == nil && open[0] == meta && bytes.Equal(open[1:17], credentials.Token[:]) && bytes.Equal(open[17:], []byte{192, 0, 2, 1, 1, 187})
				data := make([]byte, len(payload))
				_, err = io.ReadFull(secure, data)
				valid = valid && err == nil && bytes.Equal(data, payload)
				_, err = secure.Write(data)
				serverResult <- valid && err == nil
			}()
			testSOCKS(t, app, credentials, udp)
			if _, err := app.Write(payload); err != nil {
				t.Fatal("payload write failed")
			}
			echo := make([]byte, len(payload))
			if _, err := io.ReadFull(app, echo); err != nil || !bytes.Equal(echo, payload) {
				t.Fatal("exact echo failed")
			}
			if !<-serverResult {
				t.Fatal("FlowRelay framing or TLS payload failed")
			}
			cancel()
			<-done
		})
	}
}

func TestNoUnauthenticatedDial(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	app, local := net.Pipe()
	defer app.Close()
	app.SetDeadline(time.Now().Add(time.Second))
	bridge := &Bridge{Dial: func(context.Context) (net.Conn, error) {
		t.Error("unauthenticated carrier dial")
		return nil, io.ErrClosedPipe
	}}
	done := make(chan struct{})
	go func() { defer close(done); bridge.Serve(ctx, local) }()
	app.Write([]byte{5, 1, 0})
	var reply [2]byte
	if _, err := io.ReadFull(app, reply[:]); err != nil || reply != [2]byte{5, 255} {
		t.Fatal("missing authentication was accepted")
	}
	<-done
}
