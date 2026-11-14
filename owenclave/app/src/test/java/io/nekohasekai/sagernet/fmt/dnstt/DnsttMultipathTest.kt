package io.nekohasekai.sagernet.fmt.dnstt

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class DnsttMultipathTest {
    @Test fun singleResolverRemainsStrict() {
        val resolver = parseDnsttResolver("tcp://77.88.8.88:53")
        assertEquals(listOf(resolver), resolver.paths())
    }

    @Test fun explicitPairPreservesOrderAndTcp() {
        assertEquals(
            listOf(DnsttResolver("tcp", "77.88.8.88", 53), DnsttResolver("tcp", "77.88.8.1", 53)),
            parseDnsttResolver("TCP+MP://77.88.8.88:53").paths(),
        )
    }

    @Test fun unsupportedPairsAndUriExtrasAreRejected() {
        for (value in listOf(
            "tcp+mp://77.88.8.1:53", "tcp+mp://77.88.8.88:54", "tcp+mp://localhost:53",
            "tcp+mp://77.88.8.88:53/", "tcp+mp://77.88.8.88:53?x", "tcp+mp://77.88.8.88:53#x",
        )) assertFalse(value, isValidDnsttResolver(value))
    }

    @Test fun resolverHostsAreCanonicalizedForClientIdentity() {
        assertEquals("resolver.example", parseDnsttResolver("tcp://Resolver.Example:53").host)
        assertEquals("resolver.example", parseDnsttResolver("tcp://Resolver.Example.:53").host)
        assertEquals(
            parseDnsttResolver("tcp://[2001:db8::1]:53").host,
            parseDnsttResolver("tcp://[2001:0DB8:0:0:0:0:0:1]:53").host,
        )
    }
}
