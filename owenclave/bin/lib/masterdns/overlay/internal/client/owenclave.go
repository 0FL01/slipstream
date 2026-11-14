package client

import (
	"context"
	"errors"
	"net"
	"sync"
	"time"

	"masterdnsvpn-go/internal/flowbridge"
)

func (p *PingManager) owenclaveInterval(now int64) time.Duration {
	open := false
	p.client.streamsMu.RLock()
	for id, stream := range p.client.active_streams {
		if id != 0 && stream != nil && stream.TerminalSince().IsZero() {
			open = true
			break
		}
	}
	p.client.streamsMu.RUnlock()
	if !open {
		return 5 * time.Second
	}
	last := p.lastNonPingSentAt.Load()
	if inbound := p.lastNonPongReceivedAt.Load(); inbound > last {
		last = inbound
	}
	if now-last < int64(time.Second) {
		return 400 * time.Millisecond
	}
	return 2 * time.Second
}

// RunOwenclave leaves retries, deadlines, readiness and process ownership to
// Android. A session reset terminates this child instead of retrying invisibly.
func (c *Client) RunOwenclave(ctx context.Context, listen string, bridge *flowbridge.Bridge, ready func()) error {
	if len(c.cfg.Resolvers) != 1 || len(c.cfg.Domains) != 1 || c.cfg.ListenPort != 0 {
		return errors.New("invalid managed carrier configuration")
	}
	if err := c.RunInitialMTUTests(ctx); err != nil {
		return err
	}
	if err := c.InitializeSession(1); err != nil {
		return err
	}
	if err := c.StartAsyncRuntime(ctx); err != nil {
		return err
	}
	defer c.StopAsyncRuntime()
	c.InitVirtualStream0()
	c.pingManager.Start(ctx)
	listener, err := net.Listen("tcp", listen)
	if err != nil {
		return err
	}
	defer listener.Close()
	owner, cancel := context.WithCancel(ctx)
	defer cancel()
	stop := context.AfterFunc(owner, func() { listener.Close() })
	defer stop()
	bridge.Dial = func(ctx context.Context) (net.Conn, error) {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		local, carrier := net.Pipe()
		c.HandleTCPConnect(ctx, carrier)
		return local, nil
	}
	ready()
	var sessions sync.WaitGroup
	accepted := make(chan struct{})
	defer func() {
		cancel()
		listener.Close()
		<-accepted
		sessions.Wait()
	}()
	slots := make(chan struct{}, 128)
	errorsCh := make(chan error, 1)
	go func() {
		defer close(accepted)
		for {
			conn, err := listener.Accept()
			if err != nil {
				errorsCh <- err
				return
			}
			select {
			case slots <- struct{}{}:
				sessions.Add(1)
				go func() {
					defer sessions.Done()
					defer func() { <-slots }()
					bridge.Serve(owner, conn)
				}()
			default:
				conn.Close()
			}
		}
	}()
	select {
	case <-ctx.Done():
		return nil
	case <-c.sessionResetSignal:
		return errors.New("carrier session reset")
	case err := <-errorsCh:
		if ctx.Err() != nil {
			return nil
		}
		return err
	}
}
