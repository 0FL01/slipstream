// Package flowbridge carries authenticated FlowRelay streams inside pinned TLS.
// The underlying DNS carrier is not trusted for confidentiality or integrity.
package flowbridge

import (
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"io"
	"net"
	"time"
)

const bootstrapSize = 16 + 32 + 32

type Credentials struct {
	Token [16]byte
	User  [32]byte
	Pass  [32]byte
}

func ReadCredentials(r io.Reader) (Credentials, error) {
	var c Credentials
	b, err := io.ReadAll(io.LimitReader(r, bootstrapSize+1))
	if err != nil || len(b) != bootstrapSize {
		return c, errors.New("invalid bootstrap")
	}
	copy(c.Token[:], b[:16])
	copy(c.User[:], b[16:48])
	copy(c.Pass[:], b[48:])
	clear(b)
	return c, nil
}

// CarrierKey is separate from the FlowRelay bearer credential. This is not the
// application's encryption layer: that protection is supplied by TLS below.
func CarrierKey(token [16]byte) string {
	h := sha256.New()
	h.Write([]byte("owenclave/masterdnsvpn/carrier/v1\x00"))
	h.Write(token[:])
	return hex.EncodeToString(h.Sum(nil))
}

func TLSConfig(certificate []byte, name string) (*tls.Config, error) {
	block, rest := pem.Decode(certificate)
	if name == "" || block == nil || block.Type != "CERTIFICATE" {
		return nil, errors.New("invalid server trust")
	}
	if extra, _ := pem.Decode(rest); extra != nil {
		return nil, errors.New("ambiguous server trust")
	}
	pinned, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.New("invalid server certificate")
	}
	return &tls.Config{
		MinVersion: tls.VersionTLS13, MaxVersion: tls.VersionTLS13,
		ServerName: name,
		// The retained Slipstream certificate has no SAN. Use exact certificate
		// pinning, NOT an unauthenticated TLS connection or the host trust store.
		InsecureSkipVerify: true,
		VerifyConnection: func(state tls.ConnectionState) error {
			now := time.Now()
			if len(state.PeerCertificates) == 0 ||
				subtle.ConstantTimeCompare(state.PeerCertificates[0].Raw, pinned.Raw) != 1 ||
				now.Before(pinned.NotBefore) || now.After(pinned.NotAfter) {
				return errors.New("server certificate pin rejected")
			}
			return nil
		},
		NextProtos: []string{"owenclave-flowrelay/1"},
	}, nil
}

type Bridge struct {
	Credentials Credentials
	TLS         *tls.Config
	Dial        func(context.Context) (net.Conn, error)
}

func readByte(r io.Reader) (byte, error) {
	var b [1]byte
	_, err := io.ReadFull(r, b[:])
	return b[0], err
}

func readAddress(r io.Reader, kind byte) (byte, []byte, error) {
	n, tag := 0, byte(0)
	switch kind {
	case 1:
		n = 4
	case 4:
		n, tag = 16, 1
	case 3:
		length, err := readByte(r)
		if err != nil || length == 0 || length > 253 {
			return 0, nil, errors.New("invalid domain length")
		}
		n, tag = int(length), 2
	default:
		return 0, nil, errors.New("invalid address type")
	}
	b := make([]byte, n)
	if _, err := io.ReadFull(r, b); err != nil {
		return 0, nil, err
	}
	if tag == 2 {
		b = append([]byte{byte(n)}, b...)
	}
	return tag, b, nil
}

func (b *Bridge) open(local net.Conn) ([]byte, error) {
	var head [2]byte
	if _, err := io.ReadFull(local, head[:]); err != nil {
		return nil, err
	}
	if head[0] != 5 || head[1] == 0 {
		return nil, errors.New("invalid SOCKS greeting")
	}
	methods := make([]byte, int(head[1]))
	if _, err := io.ReadFull(local, methods); err != nil {
		return nil, err
	}
	hasAuth := false
	for _, method := range methods {
		hasAuth = hasAuth || method == 2
	}
	if !hasAuth {
		local.Write([]byte{5, 255})
		return nil, errors.New("SOCKS authentication required")
	}
	if _, err := local.Write([]byte{5, 2}); err != nil {
		return nil, err
	}
	if _, err := io.ReadFull(local, head[:]); err != nil || head[0] != 1 || head[1] != 32 {
		return nil, errors.New("invalid SOCKS authentication")
	}
	var user, pass [32]byte
	if _, err := io.ReadFull(local, user[:]); err != nil {
		return nil, err
	}
	n, err := readByte(local)
	if err != nil || n != 32 {
		return nil, errors.New("invalid SOCKS credential length")
	}
	if _, err := io.ReadFull(local, pass[:]); err != nil {
		return nil, err
	}
	valid := subtle.ConstantTimeCompare(user[:], b.Credentials.User[:]) &
		subtle.ConstantTimeCompare(pass[:], b.Credentials.Pass[:])
	clear(user[:])
	clear(pass[:])
	if valid != 1 {
		local.Write([]byte{1, 1})
		return nil, errors.New("unauthorized local connection")
	}
	if _, err := local.Write([]byte{1, 0}); err != nil {
		return nil, err
	}
	var request [4]byte
	if _, err := io.ReadFull(local, request[:]); err != nil || request[0] != 5 || request[1] != 1 || request[2] != 0 {
		return nil, errors.New("unsupported SOCKS command")
	}
	tag, address, err := readAddress(local, request[3])
	if err != nil {
		return nil, err
	}
	var port [2]byte
	if _, err := io.ReadFull(local, port[:]); err != nil {
		return nil, err
	}
	udp := tag == 2 && string(address[1:]) == "sp.v2.udp-over-tcp.arpa" && port == [2]byte{}
	if port == [2]byte{} && !udp {
		return nil, errors.New("zero destination port")
	}
	// As in the retained Slipstream adapter, UoT sends its bind preface only
	// after SOCKS success. A later carrier/destination error closes the stream.
	if _, err := local.Write([]byte{5, 0, 0, 1, 0, 0, 0, 0, 0, 0}); err != nil {
		return nil, err
	}
	command := byte(0)
	if udp {
		if _, err := io.ReadFull(local, head[:]); err != nil || head[0] != 0 {
			return nil, errors.New("only bind-mode UoT is supported")
		}
		tag, address, err = readAddress(local, head[1])
		if err != nil {
			return nil, err
		}
		if _, err := io.ReadFull(local, port[:]); err != nil || port == [2]byte{} {
			return nil, errors.New("invalid UoT port")
		}
		command = 1
	}
	open := []byte{0x20 | command<<3 | tag<<1}
	open = append(open, b.Credentials.Token[:]...)
	open = append(open, address...)
	return append(open, port[:]...), nil
}

func (b *Bridge) Serve(ctx context.Context, local net.Conn) {
	defer local.Close()
	stop := context.AfterFunc(ctx, func() { local.Close() })
	defer stop()
	local.SetDeadline(time.Now().Add(15 * time.Second))
	open, err := b.open(local)
	if err != nil {
		return
	}
	defer clear(open)
	if b.TLS == nil || b.Dial == nil {
		return
	}
	setup, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	raw, err := b.Dial(setup)
	if err != nil {
		return
	}
	defer raw.Close()
	stopRaw := context.AfterFunc(ctx, func() { raw.Close() })
	defer stopRaw()
	secure := tls.Client(raw, b.TLS)
	if err := secure.HandshakeContext(setup); err != nil || secure.ConnectionState().NegotiatedProtocol != "owenclave-flowrelay/1" {
		return
	}
	secure.SetWriteDeadline(time.Now().Add(15 * time.Second))
	if _, err := secure.Write(open); err != nil {
		return
	}
	clear(open)
	secure.SetDeadline(time.Time{})
	local.SetDeadline(time.Time{})
	Relay(local, secure)
}

// Relay preserves a graceful half-close in each direction where supported.
func Relay(a, b net.Conn) {
	done := make(chan struct{}, 1)
	copyHalf := func(dst, src net.Conn) {
		_, err := io.Copy(dst, src)
		if err != nil {
			a.Close()
			b.Close()
		} else if half, ok := dst.(interface{ CloseWrite() error }); ok {
			_ = half.CloseWrite()
		} else {
			dst.Close()
		}
	}
	go func() { copyHalf(a, b); done <- struct{}{} }()
	copyHalf(b, a)
	<-done
}
