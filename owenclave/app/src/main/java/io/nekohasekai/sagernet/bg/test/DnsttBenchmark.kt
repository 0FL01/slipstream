package io.nekohasekai.sagernet.bg.test

import android.os.SystemClock
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.bg.proto.V2RayInstance
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.fmt.dnstt.DnsttResolver
import io.nekohasekai.sagernet.fmt.dnstt.automaticDnsttResolvers
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttResolver
import io.nekohasekai.sagernet.fmt.gson.gson
import io.nekohasekai.sagernet.ktx.mkPort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Proxy
import java.net.URL

internal data class DnsttBenchmarkResult(
    val resolver: DnsttResolver,
    val label: String,
    val experimental: Boolean,
    val speedMbps: Double? = null,
    val latencyMs: Long? = null,
    val failure: String? = null,
) {
    val complete get() = failure == null && speedMbps != null && latencyMs != null
}

internal data class DnsttBenchmarkSnapshot(
    val version: Int = 1,
    val benchmarkHost: String,
    val results: List<DnsttBenchmarkResult>,
)

internal fun DnsttBenchmarkSnapshot.encode() = gson.toJson(this)

internal fun decodeDnsttBenchmarkSnapshot(value: String): DnsttBenchmarkSnapshot? = runCatching {
    gson.fromJson(value, DnsttBenchmarkSnapshot::class.java).takeIf { snapshot ->
        snapshot.version == 1 && snapshot.benchmarkHost.isNotBlank() &&
                snapshot.results.isNotEmpty() && snapshot.results.any { it.complete } &&
                snapshot.results.map { it.resolver }.distinct().size == snapshot.results.size &&
                snapshot.results.all { result ->
                    result.label.isNotBlank() &&
                            parseDnsttResolver(result.resolver.toString()) == result.resolver &&
                            (result.failure != null || result.complete) &&
                            (result.speedMbps?.let { it.isFinite() && it > 0.0 } != false) &&
                            (result.latencyMs?.let { it >= 0L } != false)
                }
    }
}.getOrNull()

internal fun List<DnsttBenchmarkResult>.sortedDnsttBenchmarkResults() = sortedWith(
    compareByDescending<DnsttBenchmarkResult> { it.complete }
        .thenByDescending { it.speedMbps ?: 0.0 }
        .thenBy { it.latencyMs ?: Long.MAX_VALUE },
)

internal class DnsttBenchmark(private val profile: ProxyEntity) {
    private companion object {
        const val PROBE_BYTES = 64L * 1024L
        const val PROBE_DURATION_MS = 1_500L
        const val RUN_DURATION_MS = 3_000L
        const val RUN_BYTE_LIMIT = 512L * 1024L
        const val CONNECT_TIMEOUT_MS = 5_000
        const val READ_TIMEOUT_MS = 1_000
        val BENCHMARK_RESOLVERS = listOf(
            BenchmarkResolver("77.88.8.8", "Yandex Basic"),
            BenchmarkResolver("77.88.8.1", "Yandex Basic"),
            BenchmarkResolver("77.88.8.88", "Yandex Safe"),
            BenchmarkResolver("77.88.8.2", "Yandex Safe"),
            BenchmarkResolver("77.88.8.7", "Yandex Family"),
            BenchmarkResolver("77.88.8.3", "Yandex Family"),
            BenchmarkResolver("84.53.201.130", "Rostelecom", experimental = true),
            BenchmarkResolver("94.25.113.230", "Rostelecom", experimental = true),
            BenchmarkResolver("82.151.127.188", "Rostelecom", experimental = true),
            BenchmarkResolver("95.167.75.62", "Rostelecom", experimental = true),
            BenchmarkResolver("95.167.26.10", "Rostelecom", experimental = true),
            BenchmarkResolver("85.237.61.86", "Rostelecom", experimental = true),
            BenchmarkResolver("217.18.135.118", "Rostelecom", experimental = true),
            BenchmarkResolver("95.167.214.38", "Rostelecom", experimental = true),
            BenchmarkResolver("95.167.150.28", "Rostelecom", experimental = true),
        )
        val BENCHMARK_HOSTS = listOf(
            BenchmarkHost("Cloudflare", "https://speed.cloudflare.com/__down", generated = true),
            BenchmarkHost("OVH", "https://proof.ovh.net/files/10Mb.dat"),
            BenchmarkHost("Hetzner", "https://fsn1-speed.hetzner.com/100MB.bin"),
        )
    }

    private data class BenchmarkResolver(
        val resolver: DnsttResolver,
        val label: String,
        val experimental: Boolean = false,
    ) {
        constructor(host: String, label: String, experimental: Boolean = false) :
                this(DnsttResolver("tcp", host, 53), label, experimental)
    }

    private data class BenchmarkHost(
        val name: String,
        val endpoint: String,
        val generated: Boolean = false,
    )

    private class BenchmarkServersUnavailableException : IOException("Benchmark servers unavailable")
    private class NetworkChangedException : IOException("Network changed; run benchmark again")

    suspend fun run(
        onUpdate: suspend (DnsttBenchmarkResult) -> Unit,
        onHostSelected: suspend (String) -> Unit,
    ): DnsttBenchmarkSnapshot {
        require(profile.type == ProxyEntity.TYPE_DNSTT)
        val network = V2RayInstance.underlayNetwork() ?: throw IOException("No physical network")
        val dnsServers = SagerNet.connectivity.getLinkProperties(network)?.dnsServers.orEmpty()
        var benchmarkHost: BenchmarkHost? = null
        var anyComplete = false
        val results = mutableListOf<DnsttBenchmarkResult>()

        for (candidateResolver in benchmarkResolvers(dnsServers)) {
            val resolver = candidateResolver.resolver
            ensureNetwork(network)
            var result = DnsttBenchmarkResult(
                resolver,
                candidateResolver.label,
                candidateResolver.experimental,
            )
            var carrierReady = false
            onUpdate(result)
            val candidate = profile.copy(
                dnsttBean = profile.dnsttBean?.clone()?.apply { this.resolver = resolver.toString() },
            )
            val httpPort = mkPort()
            val instance = V2RayTestInstance(candidate, testHttpPort = httpPort)
            try {
                instance.runTest {
                    carrierReady = true
                    val host = benchmarkHost ?: selectBenchmarkHost(httpPort).also {
                        benchmarkHost = it
                        onHostSelected(it.name)
                    }
                    ensureNetwork(network)
                    val (speed, latency) = transfer(httpPort, host, RUN_BYTE_LIMIT, RUN_DURATION_MS)
                    result = result.copy(speedMbps = speed, latencyMs = latency)
                    onUpdate(result)
                }
                ensureNetwork(network)
            } catch (error: CancellationException) {
                throw error
            } catch (_: NetworkChangedException) {
                throw NetworkChangedException()
            } catch (error: BenchmarkServersUnavailableException) {
                result = result.copy(failure = error.message)
                onUpdate(result)
            } catch (_: Throwable) {
                if (V2RayInstance.underlayNetwork() != network) throw NetworkChangedException()
                result = result.copy(
                    failure = if (carrierReady) "Benchmark transfer failed" else "DNS carrier unavailable",
                )
                onUpdate(result)
            }
            if (result.complete) anyComplete = true
            results += result
        }
        if (!anyComplete) throw IOException("No TCP DNS resolver established the tunnel")
        return DnsttBenchmarkSnapshot(benchmarkHost = requireNotNull(benchmarkHost).name, results = results)
    }

    private fun benchmarkResolvers(dnsServers: List<InetAddress>): List<BenchmarkResolver> {
        val builtIns = BENCHMARK_RESOLVERS.associateBy { it.resolver }
        val resolvers = linkedMapOf<DnsttResolver, BenchmarkResolver>()
        automaticDnsttResolvers(dnsServers).forEach { resolver ->
            resolvers[resolver] = builtIns[resolver] ?: BenchmarkResolver(resolver, "Network DNS")
        }
        BENCHMARK_RESOLVERS.forEach { resolvers.putIfAbsent(it.resolver, it) }
        return resolvers.values.toList()
    }

    private fun ensureNetwork(expected: android.net.Network) {
        if (V2RayInstance.underlayNetwork() != expected) throw NetworkChangedException()
    }

    private suspend fun selectBenchmarkHost(httpPort: Int): BenchmarkHost {
        for (host in BENCHMARK_HOSTS) {
            try {
                transfer(httpPort, host, PROBE_BYTES, PROBE_DURATION_MS)
                return host
            } catch (error: CancellationException) {
                throw error
            } catch (_: Throwable) {
                // Try the next stable public speed-test endpoint.
            }
        }
        throw BenchmarkServersUnavailableException()
    }

    private suspend fun transfer(
        httpPort: Int,
        host: BenchmarkHost,
        byteLimit: Long,
        durationMs: Long,
    ) = withContext(Dispatchers.IO) {
        val proxy = Proxy(Proxy.Type.HTTP, InetSocketAddress("127.0.0.1", httpPort))
        val nonce = SystemClock.elapsedRealtimeNanos()
        val url = URL(if (host.generated) {
            "${host.endpoint}?bytes=$byteLimit&r=$nonce"
        } else {
            "${host.endpoint}?r=$nonce"
        })
        val startedAt = SystemClock.elapsedRealtimeNanos()
        val connection = (url.openConnection(proxy) as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout = READ_TIMEOUT_MS
            useCaches = false
            setRequestProperty("Cache-Control", "no-store")
            if (!host.generated) setRequestProperty("Range", "bytes=0-${byteLimit - 1}")
        }
        try {
            if (
                connection.responseCode != HttpURLConnection.HTTP_OK &&
                connection.responseCode != HttpURLConnection.HTTP_PARTIAL
            ) throw IOException("Benchmark unavailable")
            val transferStartedAt = SystemClock.elapsedRealtimeNanos()
            val latencyMs = (transferStartedAt - startedAt) / 1_000_000L
            val deadline = transferStartedAt + durationMs * 1_000_000L
            var received = 0L
            var elapsedMs = 1L
            val buffer = ByteArray(32 * 1024)
            connection.inputStream.use { input ->
                while (received < byteLimit && SystemClock.elapsedRealtimeNanos() < deadline) {
                    val length = minOf(buffer.size.toLong(), byteLimit - received).toInt()
                    val read = input.read(buffer, 0, length)
                    if (read < 0) break
                    received += read
                }
                elapsedMs = ((SystemClock.elapsedRealtimeNanos() - transferStartedAt) / 1_000_000L)
                    .coerceAtLeast(1L)
            }
            if (received == 0L) throw IOException("Benchmark returned no data")
            (received * 8.0 / elapsedMs / 1000.0) to latencyMs
        } finally {
            connection.disconnect()
        }
    }
}
