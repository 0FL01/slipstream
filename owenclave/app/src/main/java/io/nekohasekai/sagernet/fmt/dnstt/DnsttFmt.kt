package io.nekohasekai.sagernet.fmt.dnstt

import io.nekohasekai.sagernet.ktx.joinHostPort
import java.net.InetAddress
import java.net.URI
import java.util.Locale

private val flowTokenPattern = Regex("[0-9a-f]{32}")
private val domainLabelPattern = Regex("[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?")
private val numericIpv4Pattern = Regex("[0-9.]+")
private val numericIpv6Pattern = Regex("[0-9a-fA-F:.]+")

data class DnsttProvisioning(val domain: String, val token: String) {
    fun toUri() = "owenclave-dns://$domain?token=$token"

    override fun toString() = "DnsttProvisioning(domain=$domain, token=<redacted>)"
}

fun parseDnsttDomain(value: String): String {
    require(value.isNotEmpty() && value.length <= 238 && value.all { it.code in 0x21..0x7e }) {
        "invalid DNS Tunnel domain"
    }
    val domain = value.lowercase(Locale.ROOT)
    val labels = domain.split('.')
    require(
        labels.size >= 2 && labels.all { domainLabelPattern.matches(it) } && labels.last().any(Char::isLetter),
    ) {
        "invalid DNS Tunnel domain"
    }
    require(!(labels.size == 4 && labels.all { label -> label.toIntOrNull()?.let { it in 0..255 } == true })) {
        "invalid DNS Tunnel domain"
    }
    return domain
}

fun isValidDnsttDomain(value: String) = runCatching { parseDnsttDomain(value) }.isSuccess

fun parseDnsttProvisioning(value: String, rawTokenDomain: String = DnsttBean.LEGACY_DOMAIN): DnsttProvisioning {
    val input = value.trim()
    if (isValidDnsttToken(input)) {
        return DnsttProvisioning(parseDnsttDomain(rawTokenDomain), input)
    }

    val uri = runCatching { URI(input) }.getOrElse {
        throw IllegalArgumentException("invalid DNS Tunnel provisioning key")
    }
    require(uri.scheme?.equals("owenclave-dns", ignoreCase = true) == true) {
        "invalid DNS Tunnel provisioning key"
    }
    require(
        uri.userInfo == null && uri.port == -1 && uri.path.isNullOrEmpty() && uri.fragment == null &&
            uri.rawAuthority == uri.host && uri.rawQuery?.matches(Regex("token=[0-9a-f]{32}")) == true,
    ) { "invalid DNS Tunnel provisioning key" }
    val domain = runCatching { parseDnsttDomain(uri.host.orEmpty()) }.getOrElse {
        throw IllegalArgumentException("invalid DNS Tunnel provisioning key")
    }
    val token = uri.rawQuery.substringAfter("token=")
    return DnsttProvisioning(domain, token)
}

fun isValidDnsttProvisioning(value: String, rawTokenDomain: String = DnsttBean.LEGACY_DOMAIN) =
    runCatching { parseDnsttProvisioning(value, rawTokenDomain) }.isSuccess

data class DnsttResolver(val transport: String, val host: String, val port: Int) {
    override fun toString() = "$transport://${joinHostPort(host, port)}"

    fun paths(): List<DnsttResolver> = if (transport == "tcp+mp") {
        require(host == "77.88.8.88" && port == 53) { "Unsupported multipath pair" }
        listOf(DnsttResolver("tcp", host, port), DnsttResolver("tcp", "77.88.8.1", 53))
    } else listOf(this)
}

fun automaticDnsttResolvers(dnsServers: List<InetAddress>): List<DnsttResolver> {
    val hosts = linkedSetOf<String>()
    dnsServers.forEach { address ->
        if (hosts.size < 2) address.hostAddress?.let(hosts::add)
    }
    hosts.add("77.88.8.8")
    hosts.add("77.88.8.1")
    return hosts.map { DnsttResolver("tcp", it, 53) }
}

fun parseDnsttResolver(value: String): DnsttResolver {
    val uri = URI(value.trim())
    val transport = uri.scheme?.lowercase()
    require(transport == "udp" || transport == "tcp" || transport == "tcp+mp") {
        "DNS resolver must use udp://, tcp:// or the documented tcp+mp:// pair"
    }
    require(uri.userInfo == null && uri.path.isNullOrEmpty() && uri.query == null && uri.fragment == null) {
        "invalid DNS resolver"
    }
    val rawHost = uri.host.orEmpty().removeSurrounding("[", "]")
    require(rawHost.isNotEmpty() && uri.port in 1..65535) { "DNS resolver host and port are required" }
    val host = when {
        rawHost.contains(':') && numericIpv6Pattern.matches(rawHost) ->
            InetAddress.getByName(rawHost).hostAddress
        numericIpv4Pattern.matches(rawHost) -> InetAddress.getByName(rawHost).hostAddress
        else -> rawHost.lowercase(Locale.ROOT).removeSuffix(".").also {
            require(it.isNotEmpty()) { "DNS resolver host and port are required" }
        }
    }
    return DnsttResolver(transport, host, uri.port).also { it.paths() }
}

fun isValidDnsttResolver(value: String) = runCatching { parseDnsttResolver(value) }.isSuccess

fun isValidDnsttToken(value: String) = flowTokenPattern.matches(value)

fun decodeDnsttToken(value: String): ByteArray {
    require(isValidDnsttToken(value)) { "invalid DNS Tunnel Flow token" }
    return value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
