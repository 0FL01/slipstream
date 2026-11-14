package config

// OwenclaveClientConfig does not load a resolver list, credentials, or caches
// from disk. The Android owner has already selected exactly one resolver.
func OwenclaveClientConfig(domain, host string, port int, key string) ClientConfig {
	c := defaultClientConfig()
	c.OwenclaveManaged = true
	c.ProtocolType = "TCP"
	c.Domains = []string{domain}
	c.Resolvers = []ResolverAddress{{IP: host, Port: port}}
	c.ResolverMap = map[string]int{host: port}
	c.EncryptionKey, c.DataEncryptionMethod = key, 5
	c.ListenPort = 0 // the managed owner supplies the authenticated listener
	c.LocalDNSEnabled, c.LocalDNSCachePersist = false, false
	c.SaveMTUServersToFile = false
	c.RecheckInactiveServersEnabled, c.AutoDisableTimeoutServers = false, false
	c.PacketDuplicationCount, c.SetupPacketDuplicationCount = 1, 1
	c.UploadCompressionType, c.DownloadCompressionType = 0, 0
	c.MTUTestParallelism, c.MTUTestRetries, c.MTUTestTimeout = 1, 1, 1
	c.MaxDownloadMTU = 1000
	c.RX_TX_Workers, c.TunnelProcessWorkers = 4, 2
	c.DispatcherIdlePollIntervalSeconds = 0.05
	c.PingAggressiveIntervalSeconds, c.PingLazyIntervalSeconds = 0.4, 0.4
	c.PingCooldownIntervalSeconds, c.PingColdIntervalSeconds = 2, 5
	c.PingWarmThresholdSeconds, c.PingCoolThresholdSeconds, c.PingColdThresholdSeconds = 0.4, 1, 30
	return c
}
