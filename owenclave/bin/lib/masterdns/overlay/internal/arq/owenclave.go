package arq

// LimitSendWindow reduces local admission without shrinking the peer receive
// window. Managed DNS clients call this before Start; it never increases a limit.
func (a *ARQ) LimitSendWindow(limit int) {
	if limit < 1 {
		return
	}
	a.mu.Lock()
	a.limit = min(a.limit, limit)
	a.mu.Unlock()
}
