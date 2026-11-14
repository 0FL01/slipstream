package io.nekohasekai.sagernet.ktx

import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.olcrtc.OLCRTCBean
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FormatsTest {
    @Test fun mixedCanonicalLinksRetainBothProfileTypes() {
        val token = "0123456789abcdef".repeat(2)
        val dns = "owenclave-dns://Tunnel.Example?token=$token"
        val olcrtc = "olcrtc://" + "jitsi?datachannel@room#key"

        val beans = parseShareLinks("$dns\n$olcrtc")

        assertEquals(2, beans.size)
        assertTrue(beans[0] is DnsttBean)
        assertEquals("tunnel.example", (beans[0] as DnsttBean).serverAddress)
        assertEquals(token, (beans[0] as DnsttBean).token)
        assertTrue(beans[1] is OLCRTCBean)
    }
}
