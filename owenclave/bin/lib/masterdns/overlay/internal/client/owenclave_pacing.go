package client

import (
	"time"

	Enums "masterdnsvpn-go/internal/enums"
)

// A response can deliver data only when the client supplies a DNS question.
// Reliability ACKs alone do not create a sufficiently deep downstream pipeline.
// Only active application streams get speculative questions; idle phases retain
// their original bounds. Expired observations must not permanently stop polling.
func (b *Balancer) owenclaveFreshPending(now time.Time) int {
	count := 0
	for i := range b.pendingShards {
		shard := &b.pendingShards[i]
		shard.mu.Lock()
		for _, sample := range shard.pending {
			if now.Sub(sample.sentAt) < time.Second {
				count++
			}
		}
		shard.mu.Unlock()
		if count >= 32 {
			break
		}
	}
	return count
}

func owenclavePollBudget(phase time.Duration, idle time.Duration, pending, queued int, since time.Duration) (int, time.Duration) {
	interval, count := phase, 1
	if phase == 400*time.Millisecond && idle < 400*time.Millisecond {
		interval = 50 * time.Millisecond
		count = min(16, max(0, 32-pending-queued))
		if count == 0 && since >= 400*time.Millisecond {
			count = 1
		}
	}
	if since < interval {
		return 0, interval - since
	}
	return count, interval
}

func (p *PingManager) owenclaveLoop() {
	p.lastWokeAt.Store(0)
	timer := time.NewTimer(100 * time.Millisecond)
	defer timer.Stop()
	lastQueued := time.Now()
	for {
		select {
		case <-p.ctx.Done():
			return
		case <-p.wakeCh:
		case <-timer.C:
		}
		now := time.Now()
		phase := p.owenclaveInterval(now.UnixNano())
		lastActive := p.lastNonPingSentAt.Load()
		if received := p.lastNonPongReceivedAt.Load(); received > lastActive {
			lastActive = received
		}
		p.client.streamsMu.RLock()
		s0 := p.client.active_streams[0]
		p.client.streamsMu.RUnlock()
		queued := len(p.client.plannerQueue) + len(p.client.encodedTXChannel)
		if s0 != nil {
			queued += s0.txQueue.FastSize()
		}
		pending := p.client.balancer.owenclaveFreshPending(now)
		count, wait := owenclavePollBudget(phase, time.Duration(now.UnixNano()-lastActive), pending, queued, now.Sub(lastQueued))
		if p.client.SessionReady() && s0 != nil {
			for i := 0; i < count; i++ {
				payload, err := buildClientPingPayload()
				if err != nil {
					break
				}
				if s0.PushTXPacket(Enums.DefaultPacketPriority(Enums.PACKET_PING), Enums.PACKET_PING,
					p.nextPingSequence(), 0, 0, 0, 0, payload) {
					lastQueued = now
				}
			}
		}
		if !timer.Stop() {
			select {
			case <-timer.C:
			default:
			}
		}
		timer.Reset(wait)
	}
}
