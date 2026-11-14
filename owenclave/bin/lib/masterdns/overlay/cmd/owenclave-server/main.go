package main

import (
	"context"
	"errors"
	"flag"
	"io"
	"os"
	"os/signal"
	"syscall"

	"masterdnsvpn-go/internal/config"
	"masterdnsvpn-go/internal/flowbridge"
	"masterdnsvpn-go/internal/logger"
	"masterdnsvpn-go/internal/security"
	UDPServer "masterdnsvpn-go/internal/udpserver"
)

func run() error {
	flags := flag.NewFlagSet("masterdns-server", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	path := flags.String("config", "", "configuration file")
	token := flags.String("token-file", "", "FlowRelay token credential")
	check := flags.Bool("check", false, "validate without listening")
	if err := flags.Parse(os.Args[1:]); err != nil {
		return err
	}
	cfg, err := config.LoadServerConfig(*path)
	if err != nil {
		return err
	}
	if flags.NArg() != 0 || cfg.ProtocolType != "TCP" || cfg.UseExternalSOCKS5 || cfg.FallbackAddress != "" ||
		cfg.ForwardIP != "10.200.0.2" || cfg.ForwardPort != 40002 || cfg.DataEncryptionMethod != 5 ||
		len(cfg.Domain) != 1 || cfg.Domain[0] != "t.x.ass-peak.de" {
		return errors.New("unsafe carrier configuration")
	}
	f, err := os.Open(*token)
	if err != nil {
		return err
	}
	defer f.Close()
	value, err := io.ReadAll(io.LimitReader(f, 17))
	if err != nil || len(value) != 16 {
		return errors.New("invalid token credential")
	}
	var secret [16]byte
	copy(secret[:], value)
	clear(value)
	codec, err := security.NewCodec(5, flowbridge.CarrierKey(secret))
	clear(secret[:])
	if err != nil {
		return err
	}
	if *check {
		return nil
	}
	cfg.OwenclaveFixedTarget = true
	srv := UDPServer.New(cfg, logger.OwenclaveSilent(), codec)
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()
	err = srv.Run(ctx)
	if ctx.Err() != nil {
		return nil
	}
	return err
}

func main() {
	if run() != nil {
		os.Exit(1)
	}
}
