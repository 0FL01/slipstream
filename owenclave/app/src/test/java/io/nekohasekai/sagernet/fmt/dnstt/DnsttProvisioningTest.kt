package io.nekohasekai.sagernet.fmt.dnstt

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class DnsttProvisioningTest {
    private val token = "0123456789abcdef".repeat(2)

    @Test fun canonicalUriRoundTripsAndNormalizesDomain() {
        val provisioning = parseDnsttProvisioning("owenclave-dns://Tunnel.Example?token=$token")

        assertEquals("tunnel.example", provisioning.domain)
        assertEquals(token, provisioning.token)
        assertEquals(provisioning, parseDnsttProvisioning(provisioning.toUri()))
    }

    @Test fun rawTokenUsesRequestedExistingDomain() {
        assertEquals(
            DnsttProvisioning("custom.example", token),
            parseDnsttProvisioning("  $token  ", "CUSTOM.EXAMPLE"),
        )
    }

    @Test fun domainValidationIsStrict() {
        assertEquals("xn--e1afmkfd.example", parseDnsttDomain("XN--E1AFMKFD.Example"))
        val longestSupported = List(3) { "a".repeat(63) }.plus("a".repeat(46)).joinToString(".")
        val tooLong = List(3) { "a".repeat(63) }.plus("a".repeat(47)).joinToString(".")
        assertEquals(238, longestSupported.length)
        assertEquals(239, tooLong.length)
        assertTrue(isValidDnsttDomain(longestSupported))
        for (value in listOf(
            "localhost", "127.0.0.1", "a..example", "-a.example", "a-.example",
            "a_example.test", "a.example.", "пример.рф", "a example.test",
            "${"a".repeat(64)}.example", "example.123", tooLong,
        )) assertFalse(value, isValidDnsttDomain(value))
    }

    @Test fun unsupportedOrAmbiguousUriPartsAreRejectedWithoutEchoingInput() {
        val invalid = listOf(
            "dnstt://tunnel.example?token=$token",
            "owenclave-dns://user@tunnel.example?token=$token",
            "owenclave-dns://tunnel.example:53?token=$token",
            "owenclave-dns://tunnel.example/?token=$token",
            "owenclave-dns://tunnel.example?token=$token#fragment",
            "owenclave-dns://tunnel.example?token=$token&extra=1",
            "owenclave-dns://tunnel.example?token=$token&token=$token",
            "owenclave-dns://tunnel.example?TOKEN=$token",
            "owenclave-dns://tunnel.example?token=${token.uppercase()}",
            "owenclave-dns://tunnel.example?token=%30${token.drop(1)}",
        )

        invalid.forEach { value ->
            assertFalse(value, isValidDnsttProvisioning(value))
            try {
                parseDnsttProvisioning(value)
                fail("accepted invalid provisioning input")
            } catch (error: IllegalArgumentException) {
                assertFalse(error.message.orEmpty().contains(token))
                assertFalse(error.message.orEmpty().contains(value))
            }
        }
    }

    @Test fun tokenValidationRemainsLowercaseAndExact() {
        assertTrue(isValidDnsttToken(token))
        assertFalse(isValidDnsttToken(token.uppercase()))
        assertFalse(isValidDnsttToken(token.dropLast(1)))
    }

    @Test fun diagnosticStringRedactsToken() {
        val text = DnsttProvisioning("tunnel.example", token).toString()
        assertTrue(text.contains("tunnel.example"))
        assertFalse(text.contains(token))
    }
}
