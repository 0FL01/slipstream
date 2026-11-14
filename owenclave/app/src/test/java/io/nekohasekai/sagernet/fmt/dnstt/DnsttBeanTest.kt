package io.nekohasekai.sagernet.fmt.dnstt

import com.esotericsoftware.kryo.KryoException
import io.nekohasekai.sagernet.fmt.KryoConverters
import io.nekohasekai.sagernet.ktx.byteBuffer
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class DnsttBeanTest {
    private val token = "0123456789abcdef".repeat(2)

    @Test fun legacyVersionsUseHistoricalDomainWithoutConsumingMetadata() {
        for (version in 0..1) {
            val bean = KryoConverters.dnsttDeserialize(legacyBytes(version))

            assertEquals(DnsttBean.LEGACY_DOMAIN, bean.serverAddress)
            assertEquals(53, bean.serverPort)
            assertEquals(token, bean.token)
            assertEquals("tcp://77.88.8.8:53", bean.resolver)
            assertEquals(if (version == 1) "snapshot" else "", bean.benchmarkSnapshot)
            assertEquals("Legacy profile", bean.name)
        }
    }

    @Test fun customDomainSurvivesDefaultsSerializationAndClone() {
        val original = DnsttBean().apply {
            serverAddress = "custom.example"
            token = this@DnsttBeanTest.token
            resolver = ""
            benchmarkSnapshot = "snapshot"
            name = "Custom profile"
            initializeDefaultValues()
            initializeDefaultValues()
        }

        val restored = KryoConverters.dnsttDeserialize(KryoConverters.serialize(original))
        val cloned = original.clone()

        for (bean in listOf(restored, cloned)) {
            assertEquals("custom.example", bean.serverAddress)
            assertEquals("custom.example", bean.finalAddress)
            assertEquals(53, bean.serverPort)
            assertEquals(token, bean.token)
            assertEquals("snapshot", bean.benchmarkSnapshot)
            assertEquals("Custom profile", bean.name)
        }
    }

    @Test fun domainParticipatesInBeanIdentity() {
        val first = bean("first.example")
        val second = bean("second.example")

        assertNotEquals(first, second)
    }

    @Test fun unknownPayloadVersionIsRejectedBeforeReadingFields() {
        val bytes = ByteArrayOutputStream().also { stream ->
            stream.byteBuffer().use { it.writeInt(3) }
        }.toByteArray()

        ByteArrayInputStream(bytes).byteBuffer().use { input ->
            assertThrows(KryoException::class.java) { DnsttBean().deserialize(input) }
        }
        assertThrows(DnsttBean.UnsupportedVersionException::class.java) {
            KryoConverters.dnsttDeserialize(bytes)
        }
    }

    private fun bean(domain: String) = DnsttBean().apply {
        serverAddress = domain
        token = this@DnsttBeanTest.token
        resolver = ""
        benchmarkSnapshot = ""
        initializeDefaultValues()
    }

    private fun legacyBytes(version: Int): ByteArray {
        val bytes = ByteArrayOutputStream()
        bytes.byteBuffer().use { output ->
            output.writeInt(version)
            output.writeString(token)
            output.writeString("tcp://77.88.8.8:53")
            if (version >= 1) output.writeString("snapshot")
            output.writeInt(2)
            output.writeString("Legacy profile")
            output.writeInt(0)
        }
        return bytes.toByteArray()
    }
}
