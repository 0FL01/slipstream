package io.nekohasekai.sagernet.ktx

import io.nekohasekai.sagernet.fmt.AbstractBean
import io.nekohasekai.sagernet.fmt.Serializable
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttProvisioning
import io.nekohasekai.sagernet.fmt.olcrtc.parseOLCRTCLink

fun parseShareLinks(text: String): List<AbstractBean> = text
    .lineSequence()
    .flatMap { it.trim().splitToSequence(' ') }
    .mapNotNull { value ->
        when {
            value.startsWith("olcrtc://", ignoreCase = true) ->
                runCatching { parseOLCRTCLink(value) }.getOrNull()
            value.startsWith("owenclave-dns://", ignoreCase = true) -> runCatching {
                parseDnsttProvisioning(value).let { provisioning ->
                    DnsttBean().apply {
                        serverAddress = provisioning.domain
                        token = provisioning.token
                    }
                }
            }.getOrNull()
            else -> null
        }
    }
    .toList()

fun <T : Serializable> T.applyDefaultValues(): T {
    initializeDefaultValues()
    return this
}
