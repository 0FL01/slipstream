package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"strconv"
	"syscall"

	"masterdnsvpn-go/internal/client"
	"masterdnsvpn-go/internal/config"
	"masterdnsvpn-go/internal/flowbridge"
	"masterdnsvpn-go/internal/logger"
	"masterdnsvpn-go/internal/security"
)

func run() error {
	flags := flag.NewFlagSet("masterdns", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	port := flags.Int("port", 0, "authenticated loopback port")
	domain := flags.String("domain", "", "delegated domain")
	resolver := flags.String("resolver", "", "one resolver endpoint")
	certificate := flags.String("cert", "", "pinned public certificate")
	if err := flags.Parse(os.Args[1:]); err != nil {
		return err
	}
	if *port < 1 || *port > 65535 || *domain != "t.x.ass-peak.de" || flags.NArg() != 0 {
		return fmt.Errorf("invalid arguments")
	}
	host, service, err := net.SplitHostPort(*resolver)
	if err != nil {
		return err
	}
	p, err := strconv.Atoi(service)
	if err != nil || p < 1 || p > 65535 {
		return fmt.Errorf("invalid resolver")
	}
	credentials, err := flowbridge.ReadCredentials(os.Stdin)
	if err != nil {
		return err
	}
	cert, err := os.ReadFile(*certificate)
	if err != nil {
		return err
	}
	trust, err := flowbridge.TLSConfig(cert, *domain)
	if err != nil {
		return err
	}
	cfg := config.OwenclaveClientConfig(*domain, host, p, flowbridge.CarrierKey(credentials.Token))
	codec, err := security.NewCodec(cfg.DataEncryptionMethod, cfg.EncryptionKey)
	if err != nil {
		return err
	}
	c := client.New(cfg, logger.OwenclaveSilent(), codec)
	if err := c.BuildConnectionMap(); err != nil {
		return err
	}
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()
	bridge := &flowbridge.Bridge{Credentials: credentials, TLS: trust}
	return c.RunOwenclave(ctx, net.JoinHostPort("127.0.0.1", strconv.Itoa(*port)), bridge, func() {
		fmt.Println("{\"event\":\"ready\",\"version\":1}")
	})
}

func main() {
	if run() != nil {
		os.Exit(1)
	}
}
