package arq

import "testing"

func TestLimitSendWindow(t *testing.T) {
	a := NewARQ(1, 1, nil, nil, 109, nil, Config{WindowSize: 600})
	defer a.cancel()
	window, receive := a.windowSize, a.receiveWindowSize
	a.LimitSendWindow(48)
	if a.limit != 48 || a.windowSize != window || a.receiveWindowSize != receive {
		t.Fatal("send admission or receive window changed incorrectly")
	}
	a.LimitSendWindow(600)
	a.LimitSendWindow(0)
	if a.limit != 48 {
		t.Fatal("limit increased or disabled")
	}
}
