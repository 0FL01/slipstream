package client

import (
	"testing"
	"time"
)

func TestOwenclavePollBudget(t *testing.T) {
	cases := []struct {
		name                   string
		phase, idle, since     time.Duration
		pending, queued, count int
		wait                   time.Duration
	}{
		{"busy", 400 * time.Millisecond, 0, 50 * time.Millisecond, 0, 0, 16, 50 * time.Millisecond},
		{"bounded", 400 * time.Millisecond, 0, 50 * time.Millisecond, 26, 3, 3, 50 * time.Millisecond},
		{"full", 400 * time.Millisecond, 0, 50 * time.Millisecond, 32, 0, 0, 50 * time.Millisecond},
		{"keepalive", 400 * time.Millisecond, 0, 400 * time.Millisecond, 32, 0, 1, 50 * time.Millisecond},
		{"early", 400 * time.Millisecond, 0, 10 * time.Millisecond, 0, 0, 0, 40 * time.Millisecond},
		{"warm", 400 * time.Millisecond, 500 * time.Millisecond, 400 * time.Millisecond, 0, 0, 1, 400 * time.Millisecond},
		{"quiet", 2 * time.Second, 3 * time.Second, 2 * time.Second, 0, 0, 1, 2 * time.Second},
		{"empty-early", 5 * time.Second, 0, time.Second, 0, 0, 0, 4 * time.Second},
		{"empty-keepalive", 5 * time.Second, 0, 5 * time.Second, 0, 0, 1, 5 * time.Second},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			count, wait := owenclavePollBudget(tc.phase, tc.idle, tc.pending, tc.queued, tc.since)
			if count != tc.count || wait != tc.wait {
				t.Fatalf("got count=%d wait=%s", count, wait)
			}
		})
	}
}
