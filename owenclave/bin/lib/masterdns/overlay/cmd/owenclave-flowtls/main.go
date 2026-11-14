package main

import (
	"context"
	"crypto/tls"
	"errors"
	"flag"
	"io"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"masterdnsvpn-go/internal/flowbridge"
)

func run() error {
	flags := flag.NewFlagSet("flowtls", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	cert := flags.String("cert", "", "server certificate")
	key := flags.String("key", "", "server key credential")
	check := flags.Bool("check", false, "validate without listening")
	if err := flags.Parse(os.Args[1:]); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return errors.New("unexpected arguments")
	}
	pair, err := tls.LoadX509KeyPair(*cert, *key)
	if err != nil {
		return err
	}
	if *check {
		return nil
	}
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()
	listener, err := net.Listen("tcp", "10.200.0.2:40002")
	if err != nil {
		return err
	}
	defer listener.Close()
	stop := context.AfterFunc(ctx, func() { listener.Close() })
	defer stop()
	trust := &tls.Config{MinVersion: tls.VersionTLS13, MaxVersion: tls.VersionTLS13,
		Certificates: []tls.Certificate{pair}, NextProtos: []string{"owenclave-flowrelay/1"}}
	slots := make(chan struct{}, 128)
	var sessions sync.WaitGroup
	defer sessions.Wait()
	defer cancel()
	for {
		conn, err := listener.Accept()
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
		select {
		case slots <- struct{}{}:
			sessions.Add(1)
			go func() {
				defer sessions.Done()
				defer func() { <-slots }()
				defer conn.Close()
				stop := context.AfterFunc(ctx, func() { conn.Close() })
				defer stop()
				secure := tls.Server(conn, trust)
				setup, done := context.WithTimeout(ctx, 15*time.Second)
				defer done()
				if err := secure.HandshakeContext(setup); err != nil || secure.ConnectionState().NegotiatedProtocol != "owenclave-flowrelay/1" {
					return
				}
				// Only the existing private authenticated FlowRelay is reachable.
				remote, err := (&net.Dialer{}).DialContext(setup, "tcp", "10.200.0.2:40001")
				if err != nil {
					return
				}
				defer remote.Close()
				stopRemote := context.AfterFunc(ctx, func() { remote.Close() })
				defer stopRemote()
				flowbridge.Relay(secure, remote)
			}()
		default:
			conn.Close()
		}
	}
}

func main() {
	if run() != nil {
		os.Exit(1)
	}
}
