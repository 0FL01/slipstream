package main

import (
	"bytes"
	"context"
	"crypto/subtle"
	"encoding/binary"
	"errors"
	"flag"
	"io"
	"net"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

const (
	flowVersion    = 1
	commandTCP     = 0
	commandUDP     = 1
	addressIPv4    = 0
	addressIPv6    = 1
	addressDomain  = 2
	defaultTimeout = 5 * time.Second
)

type config struct {
	listen           string
	tokenFile        string
	device           string
	resolver         string
	maxSessions      int
	headerTimeout    time.Duration
	connectTimeout   time.Duration
	check            bool
	denyPrivate      bool
	testTokenFile    string
	backendTokenFile string
	testBackend      string
	testDevice       string
}

type destination struct {
	host string
	port uint16
}

type flowOpen struct {
	command     byte
	destination destination
	testHeader  []byte
}

type testDispatch struct {
	token, backendToken [16]byte
	dial                func(context.Context) (*net.TCPConn, error)
	limit               chan struct{}
}

type dialDestination func(context.Context, destination) (*net.TCPConn, error)
type resolveDestination func(context.Context, destination) ([]net.IPAddr, error)
type listenUDP func(context.Context) (*net.UDPConn, error)

type server struct {
	config    config
	token     [16]byte
	dial      dialDestination
	resolve   resolveDestination
	listenUDP listenUDP
	test      *testDispatch
}

func main() {
	if run(os.Args[1:]) != nil {
		os.Exit(1)
	}
}

func run(args []string) error {
	cfg, err := parseConfig(args)
	if err != nil {
		return err
	}
	token, err := readToken(cfg.tokenFile)
	if err != nil {
		return err
	}
	if _, err := net.InterfaceByName(cfg.device); err != nil {
		return err
	}
	test, err := loadTestDispatch(cfg, token)
	if err != nil {
		return err
	}
	dial, resolve, listenUDP, err := newWarpNetwork(cfg.device, cfg.resolver, cfg.denyPrivate)
	if err != nil {
		return err
	}
	if cfg.check {
		return nil
	}
	listener, err := net.Listen("tcp", cfg.listen)
	if err != nil {
		return err
	}
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	return (&server{
		config: cfg, token: token, dial: dial, resolve: resolve, listenUDP: listenUDP, test: test,
	}).serve(ctx, listener)
}

func parseConfig(args []string) (config, error) {
	cfg := config{headerTimeout: defaultTimeout, connectTimeout: 10 * time.Second}
	flags := flag.NewFlagSet("flowd", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	flags.StringVar(&cfg.listen, "listen", "10.200.0.2:40001", "private listen address")
	flags.StringVar(&cfg.tokenFile, "token-file", "", "16-byte token file")
	flags.StringVar(&cfg.device, "interface", "CloudflareWARP", "outbound interface")
	flags.StringVar(&cfg.resolver, "resolver", "1.1.1.1:53", "fixed DNS resolver")
	flags.IntVar(&cfg.maxSessions, "max-sessions", 128, "maximum concurrent sessions")
	flags.BoolVar(&cfg.check, "check", false, "validate configuration without listening")
	flags.BoolVar(&cfg.denyPrivate, "deny-private", false, "reject non-public payload destinations")
	flags.StringVar(&cfg.testTokenFile, "test-token-file", "", "optional test token credential file")
	flags.StringVar(&cfg.backendTokenFile, "test-backend-token-file", "", "separate backend credential file")
	flags.StringVar(&cfg.testBackend, "test-backend", "", "fixed private test backend address")
	flags.StringVar(&cfg.testDevice, "test-interface", "", "interface for the fixed test backend only")
	if err := flags.Parse(args); err != nil || flags.NArg() != 0 {
		return config{}, errors.New("invalid arguments")
	}
	if cfg.tokenFile == "" || cfg.device == "" || cfg.maxSessions < 1 {
		return config{}, errors.New("invalid configuration")
	}
	if err := validatePrivateAddress(cfg.listen); err != nil {
		return config{}, err
	}
	if err := validateResolver(cfg.resolver); err != nil {
		return config{}, err
	}
	options := 0
	for _, value := range []string{cfg.testTokenFile, cfg.backendTokenFile, cfg.testBackend, cfg.testDevice} {
		if value != "" {
			options++
		}
	}
	if options != 0 {
		if options != 4 || cfg.testBackend == cfg.listen {
			return config{}, errors.New("incomplete or recursive test dispatch")
		}
		if err := validatePrivateAddress(cfg.testBackend); err != nil {
			return config{}, err
		}
		if err := validateResolver(cfg.testBackend); err != nil {
			return config{}, err
		}
	}
	return cfg, nil
}

func loadTestDispatch(cfg config, productionToken [16]byte) (*testDispatch, error) {
	if cfg.testTokenFile == "" {
		return nil, nil
	}
	token, err := readToken(cfg.testTokenFile)
	if err != nil {
		return nil, err
	}
	backendToken, err := readToken(cfg.backendTokenFile)
	if err != nil {
		return nil, err
	}
	if token == productionToken || backendToken == productionToken || token == backendToken {
		return nil, errors.New("test credentials must be distinct")
	}
	if _, err := net.InterfaceByName(cfg.testDevice); err != nil {
		return nil, err
	}
	dialer := &net.Dialer{Control: bindToDevice(cfg.testDevice)}
	return &testDispatch{
		token: token, backendToken: backendToken, limit: make(chan struct{}, 16),
		dial: func(ctx context.Context) (*net.TCPConn, error) {
			conn, err := dialer.DialContext(ctx, "tcp", cfg.testBackend)
			if err != nil {
				return nil, err
			}
			return conn.(*net.TCPConn), nil
		},
	}, nil
}

func validatePrivateAddress(address string) error {
	host, port, err := net.SplitHostPort(address)
	if err != nil || port == "0" {
		return errors.New("invalid listen address")
	}
	ip := net.ParseIP(host)
	if ip == nil || (!ip.IsPrivate() && !ip.IsLoopback()) {
		return errors.New("listen address must be private")
	}
	return nil
}

func validateResolver(address string) error {
	host, port, err := net.SplitHostPort(address)
	if err != nil || net.ParseIP(host) == nil {
		return errors.New("resolver must be a numeric address")
	}
	value, err := strconv.ParseUint(port, 10, 16)
	if err != nil || value == 0 {
		return errors.New("invalid resolver port")
	}
	return nil
}

func readToken(path string) ([16]byte, error) {
	var token [16]byte
	value, err := os.ReadFile(path)
	if err != nil {
		return token, err
	}
	if len(value) != len(token) {
		return token, errors.New("token must contain exactly 16 bytes")
	}
	copy(token[:], value)
	return token, nil
}

func newWarpNetwork(
	device, resolverAddress string, denyPrivate bool,
) (dialDestination, resolveDestination, listenUDP, error) {
	if err := validateResolver(resolverAddress); err != nil {
		return nil, nil, nil, err
	}
	control := bindToDevice(device)
	resolverDialer := &net.Dialer{Control: control}
	resolver := &net.Resolver{
		PreferGo:     true,
		StrictErrors: true,
		Dial: func(ctx context.Context, network, _ string) (net.Conn, error) {
			if strings.HasPrefix(network, "tcp") {
				network = "tcp"
			} else {
				network = "udp"
			}
			return resolverDialer.DialContext(ctx, network, resolverAddress)
		},
	}
	dialer := &net.Dialer{Control: control}
	resolve := func(ctx context.Context, target destination) ([]net.IPAddr, error) {
		addresses := []net.IPAddr{{IP: net.ParseIP(target.host)}}
		if addresses[0].IP == nil {
			resolved, err := resolver.LookupIPAddr(ctx, target.host)
			if err != nil {
				return nil, err
			}
			addresses = deduplicateAddresses(resolved, 16)
		}
		if len(addresses) == 0 {
			return nil, errors.New("destination did not resolve")
		}
		if denyPrivate {
			for _, address := range addresses {
				ip := address.IP
				v4 := ip.To4()
				if !ip.IsGlobalUnicast() || ip.IsPrivate() ||
					(v4 != nil && v4[0] == 100 && v4[1]&0xc0 == 64) {
					return nil, errors.New("non-public destination")
				}
			}
		}
		return addresses, nil
	}
	dial := func(ctx context.Context, target destination) (*net.TCPConn, error) {
		addresses, err := resolve(ctx, target)
		if err != nil {
			return nil, err
		}
		var lastErr error
		for _, address := range addresses {
			conn, err := dialer.DialContext(ctx, "tcp", net.JoinHostPort(address.IP.String(), strconv.Itoa(int(target.port))))
			if err == nil {
				return conn.(*net.TCPConn), nil
			}
			lastErr = err
		}
		return nil, lastErr
	}
	openUDP := func(ctx context.Context) (*net.UDPConn, error) {
		packet, err := (&net.ListenConfig{Control: control}).ListenPacket(ctx, "udp", ":0")
		if err != nil {
			return nil, err
		}
		udp, ok := packet.(*net.UDPConn)
		if !ok {
			packet.Close()
			return nil, errors.New("unexpected UDP socket type")
		}
		return udp, nil
	}
	return dial, resolve, openUDP, nil
}

func bindToDevice(device string) func(string, string, syscall.RawConn) error {
	return func(_, _ string, raw syscall.RawConn) error {
		var socketErr error
		if err := raw.Control(func(fd uintptr) {
			socketErr = syscall.SetsockoptString(int(fd), syscall.SOL_SOCKET, syscall.SO_BINDTODEVICE, device)
		}); err != nil {
			return err
		}
		return socketErr
	}
}

func deduplicateAddresses(addresses []net.IPAddr, limit int) []net.IPAddr {
	result := make([]net.IPAddr, 0, min(len(addresses), limit))
	seen := make(map[string]struct{}, len(addresses))
	for _, address := range addresses {
		key := address.IP.String()
		if key == "<nil>" {
			continue
		}
		if _, found := seen[key]; found {
			continue
		}
		seen[key] = struct{}{}
		result = append(result, address)
		if len(result) == limit {
			break
		}
	}
	return result
}

func (s *server) serve(ctx context.Context, listener net.Listener) error {
	defer listener.Close()
	limit := make(chan struct{}, s.config.maxSessions)
	var sessions sync.Map
	var workers sync.WaitGroup
	go func() {
		<-ctx.Done()
		listener.Close()
		sessions.Range(func(key, _ any) bool {
			key.(*net.TCPConn).Close()
			return true
		})
	}()
	for {
		conn, err := listener.Accept()
		if err != nil {
			if ctx.Err() != nil {
				workers.Wait()
				return nil
			}
			return err
		}
		tcpConn := conn.(*net.TCPConn)
		select {
		case limit <- struct{}{}:
			sessions.Store(tcpConn, struct{}{})
			workers.Add(1)
			go func() {
				defer workers.Done()
				defer func() { <-limit }()
				defer sessions.Delete(tcpConn)
				defer tcpConn.Close()
				s.handle(ctx, tcpConn)
			}()
		default:
			tcpConn.Close()
		}
	}
}

func (s *server) handle(ctx context.Context, client *net.TCPConn) {
	stop := context.AfterFunc(ctx, func() { client.Close() })
	defer stop()
	var testToken *[16]byte
	if s.test != nil {
		testToken = &s.test.token
	}
	open, err := parseOpen(client, s.token, s.config.headerTimeout, testToken)
	if err != nil {
		return
	}
	if open.testHeader != nil {
		select {
		case s.test.limit <- struct{}{}:
			defer func() { <-s.test.limit }()
		default:
			return
		}
		connectCtx, cancel := context.WithTimeout(ctx, s.config.connectTimeout)
		remote, err := s.test.dial(connectCtx)
		cancel()
		if err != nil {
			return
		}
		defer remote.Close()
		stopRemote := context.AfterFunc(ctx, func() { remote.Close() })
		defer stopRemote()
		// Only authenticated test OPENs cross this boundary; never forward the production token.
		copy(open.testHeader[1:17], s.test.backendToken[:])
		if err := remote.SetWriteDeadline(time.Now().Add(s.config.headerTimeout)); err != nil {
			return
		}
		if _, err := io.Copy(remote, bytes.NewReader(open.testHeader)); err != nil {
			return
		}
		if err := remote.SetWriteDeadline(time.Time{}); err != nil {
			return
		}
		relay(client, remote)
		return
	}
	if open.command == commandUDP {
		socket, err := s.listenUDP(ctx)
		if err != nil {
			return
		}
		defer socket.Close()
		relayUDP(ctx, client, socket, s.resolve, s.config.connectTimeout)
	} else {
		connectCtx, cancel := context.WithTimeout(ctx, s.config.connectTimeout)
		remote, err := s.dial(connectCtx, open.destination)
		cancel()
		if err != nil {
			return
		}
		defer remote.Close()
		relay(client, remote)
	}
}

func parseOpen(reader net.Conn, token [16]byte, timeout time.Duration, testToken *[16]byte) (flowOpen, error) {
	if err := reader.SetReadDeadline(time.Now().Add(timeout)); err != nil {
		return flowOpen{}, err
	}
	var fixed [17]byte
	if _, err := io.ReadFull(reader, fixed[:]); err != nil {
		return flowOpen{}, err
	}
	meta := fixed[0]
	command := (meta >> 3) & 3
	if meta>>5 != flowVersion || (command != commandTCP && command != commandUDP) || meta&1 != 0 {
		return flowOpen{}, errors.New("invalid metadata")
	}
	production := subtle.ConstantTimeCompare(fixed[1:], token[:])
	test := 0
	if testToken != nil {
		test = subtle.ConstantTimeCompare(fixed[1:], testToken[:])
	}
	if production|test != 1 {
		return flowOpen{}, errors.New("unauthorized")
	}
	var header bytes.Buffer
	var destinationReader io.Reader = reader
	if test == 1 && production == 0 {
		header.Write(fixed[:])
		destinationReader = io.TeeReader(reader, &header)
	}
	target, err := readDestination(destinationReader, (meta>>1)&3)
	if err != nil {
		return flowOpen{}, err
	}
	if err := reader.SetReadDeadline(time.Time{}); err != nil {
		return flowOpen{}, err
	}
	return flowOpen{command: command, destination: target, testHeader: header.Bytes()}, nil
}

func readDestination(reader io.Reader, addressType byte) (destination, error) {
	var host string
	switch addressType {
	case addressIPv4:
		var address [4]byte
		if _, err := io.ReadFull(reader, address[:]); err != nil {
			return destination{}, err
		}
		host = net.IP(address[:]).String()
	case addressIPv6:
		var address [16]byte
		if _, err := io.ReadFull(reader, address[:]); err != nil {
			return destination{}, err
		}
		host = net.IP(address[:]).String()
	case addressDomain:
		var length [1]byte
		if _, err := io.ReadFull(reader, length[:]); err != nil || length[0] == 0 || length[0] > 253 {
			return destination{}, errors.New("invalid domain length")
		}
		name := make([]byte, int(length[0]))
		if _, err := io.ReadFull(reader, name); err != nil {
			return destination{}, err
		}
		host = string(name)
		if !validDomain(host) {
			return destination{}, errors.New("invalid domain")
		}
	default:
		return destination{}, errors.New("unsupported address type")
	}
	var port [2]byte
	if _, err := io.ReadFull(reader, port[:]); err != nil {
		return destination{}, err
	}
	value := binary.BigEndian.Uint16(port[:])
	if value == 0 {
		return destination{}, errors.New("invalid port")
	}
	return destination{host: host, port: value}, nil
}

func validDomain(name string) bool {
	name = strings.TrimSuffix(name, ".")
	if name == "" {
		return false
	}
	for _, label := range strings.Split(name, ".") {
		if len(label) == 0 || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return false
		}
		for _, char := range []byte(label) {
			if (char < 'a' || char > 'z') && (char < 'A' || char > 'Z') &&
				(char < '0' || char > '9') && char != '-' {
				return false
			}
		}
	}
	return true
}

func relay(left, right *net.TCPConn) {
	result := make(chan error, 2)
	copyHalf := func(destination, source *net.TCPConn) {
		_, err := io.Copy(destination, source)
		if err == nil {
			err = destination.CloseWrite()
		}
		result <- err
	}
	go copyHalf(right, left)
	go copyHalf(left, right)
	if err := <-result; err != nil {
		left.Close()
		right.Close()
	}
	<-result
}

func readUDPRecord(reader io.Reader) (destination, []byte, error) {
	var addressType [1]byte
	if _, err := io.ReadFull(reader, addressType[:]); err != nil {
		return destination{}, nil, err
	}
	target, err := readDestination(reader, addressType[0])
	if err != nil {
		return destination{}, nil, err
	}
	var encodedLength [2]byte
	if _, err := io.ReadFull(reader, encodedLength[:]); err != nil {
		return destination{}, nil, err
	}
	payload := make([]byte, int(binary.BigEndian.Uint16(encodedLength[:])))
	if _, err := io.ReadFull(reader, payload); err != nil {
		return destination{}, nil, err
	}
	return target, payload, nil
}

func writeUDPRecord(writer io.Writer, source *net.UDPAddr, payload []byte) error {
	if len(payload) > 0xffff || source.Port < 1 || source.Port > 0xffff {
		return errors.New("invalid UDP response")
	}
	record := make([]byte, 0, 1+16+2+2+len(payload))
	if ipv4 := source.IP.To4(); ipv4 != nil {
		record = append(record, addressIPv4)
		record = append(record, ipv4...)
	} else if ipv6 := source.IP.To16(); ipv6 != nil {
		record = append(record, addressIPv6)
		record = append(record, ipv6...)
	} else {
		return errors.New("invalid UDP response address")
	}
	record = binary.BigEndian.AppendUint16(record, uint16(source.Port))
	record = binary.BigEndian.AppendUint16(record, uint16(len(payload)))
	record = append(record, payload...)
	for len(record) > 0 {
		written, err := writer.Write(record)
		if err != nil {
			return err
		}
		if written == 0 {
			return io.ErrUnexpectedEOF
		}
		record = record[written:]
	}
	return nil
}

func relayUDP(
	ctx context.Context,
	client *net.TCPConn,
	socket *net.UDPConn,
	resolve resolveDestination,
	timeout time.Duration,
) {
	result := make(chan error, 2)
	go func() {
		for {
			target, payload, err := readUDPRecord(client)
			if err != nil {
				result <- err
				return
			}
			resolveCtx, cancel := context.WithTimeout(ctx, timeout)
			addresses, err := resolve(resolveCtx, target)
			cancel()
			if err == nil {
				_, err = socket.WriteToUDP(payload, &net.UDPAddr{
					IP: addresses[0].IP, Zone: addresses[0].Zone, Port: int(target.port),
				})
			}
			if err != nil {
				result <- err
				return
			}
		}
	}()
	go func() {
		buffer := make([]byte, 0xffff)
		for {
			length, source, err := socket.ReadFromUDP(buffer)
			if err == nil {
				err = writeUDPRecord(client, source, buffer[:length])
			}
			if err != nil {
				result <- err
				return
			}
		}
	}()
	<-result
	client.Close()
	socket.Close()
	<-result
}
