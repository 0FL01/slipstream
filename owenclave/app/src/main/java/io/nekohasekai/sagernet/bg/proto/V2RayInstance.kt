/******************************************************************************
 *                                                                            *
 * Copyright (C) 2021 by nekohasekai <contact-sagernet@sekai.icu>             *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify       *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation, either version 3 of the License, or          *
 *  (at your option) any later version.                                       *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.       *
 *                                                                            *
 ******************************************************************************/

package io.nekohasekai.sagernet.bg.proto

import android.net.NetworkCapabilities
import android.os.SystemClock
import io.nekohasekai.sagernet.RootCAProvider
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.Key
import io.nekohasekai.sagernet.TunImplementation
import io.nekohasekai.sagernet.bg.AbstractInstance
import io.nekohasekai.sagernet.bg.GuardedProcessPool
import io.nekohasekai.sagernet.database.DataStore
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.fmt.LOCALHOST
import io.nekohasekai.sagernet.fmt.DnsttClientConfig
import io.nekohasekai.sagernet.fmt.V2rayBuildResult
import io.nekohasekai.sagernet.fmt.buildV2RayConfig
import io.nekohasekai.sagernet.fmt.dnstt.automaticDnsttResolvers
import io.nekohasekai.sagernet.fmt.olcrtc.OLCRTCBean
import io.nekohasekai.sagernet.ktx.*
import kotlinx.coroutines.*
import libexclavecore.V2RayInstance
import java.io.File
import java.io.IOException
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket

    abstract class V2RayInstance(
        val profile: ProxyEntity,
    ) : AbstractInstance {

        companion object {
            // How long to keep the connecting phase while waiting for an
            // external engine's local SOCKS port to come up.
            private const val READINESS_TIMEOUT_MS = 15_000L
            private const val READINESS_POLL_INTERVAL_MS = 250L
            private const val READINESS_CONNECT_TIMEOUT_MS = 500
            private const val DNS_TUNNEL_READY_TIMEOUT_MS = 15_000L

            internal fun underlayNetwork() = SagerNet.connectivity.let { connectivity ->
                val networks = listOfNotNull(connectivity.activeNetwork, SagerNet.currentNetwork) +
                    connectivity.allNetworks
                networks.distinct().firstOrNull {
                    val capabilities = connectivity.getNetworkCapabilities(it) ?: return@firstOrNull false
                    capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                        !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
                }
            }

            internal fun underlayDnsServers(): List<InetAddress> {
                val connectivity = SagerNet.connectivity
                val network = underlayNetwork() ?: return emptyList()
                return connectivity.getLinkProperties(network)?.dnsServers.orEmpty()
            }
        }

        protected open val dnsTunnelReadyTimeoutMs = DNS_TUNNEL_READY_TIMEOUT_MS
        protected open fun onDnsTunnelStopped(error: IOException) {}

        private fun underlayDnsServer(): String? {
            val dnsServers = underlayDnsServers()
            val address = (dnsServers.firstOrNull { it is Inet4Address } ?: dnsServers.firstOrNull())
                ?.hostAddress ?: return null
            return if (address.contains(':')) "[$address]:53" else "$address:53"
        }

        protected fun buildOlcrtcYaml(bean: OLCRTCBean, port: Int, username: String, password: String): String {
            // olcrtc resolves a relative `data` dir against the executable dir
            // (read-only nativeLibraryDir on Android), so use an absolute writable path.
            val dataDir = File(SagerNet.application.noBackupFilesDir, "olcrtc_data").apply { mkdirs() }
            val dnsServer = underlayDnsServer() ?: bean.dnsServer
            return buildString {
                appendLine("mode: cnc")
                appendLine("auth:")
                appendLine("  provider: ${bean.authProvider}")
                appendLine("room:")
                appendLine("  id: \"${bean.roomId}\"")
                appendLine("crypto:")
                appendLine("  key: \"${bean.encryptionKey}\"")
                appendLine("net:")
                appendLine("  transport: ${bean.transport}")
                appendLine("  dns: \"$dnsServer\"")
                appendLine("socks:")
                appendLine("  host: \"127.0.0.1\"")
                appendLine("  port: $port")
                if (username.isNotEmpty()) appendLine("  user: \"$username\"")
                if (password.isNotEmpty()) appendLine("  pass: \"$password\"")
                appendLine("data: \"${dataDir.absolutePath}\"")
                appendLine("debug: false")
            }
        }

    lateinit var config: V2rayBuildResult
    lateinit var v2rayPoint: V2RayInstance
    fun pointOrNull() = if (::v2rayPoint.isInitialized) v2rayPoint else null

    // ProxyInstance probes this legacy view for debug output. Keep it empty so
    // sidecar configuration is never exposed outside this instance.
    val pluginConfigs: Map<Int, Pair<Int, String>> get() = emptyMap()
    private val olcrtcConfigs = hashMapOf<Int, String>()
    private var dnsTunnelConfig: DnsttClientConfig? = null
    private var dnsTunnelInstance: SlipstreamInstance? = null

    // Local SOCKS ports of external engines that need to finish bringing up
    // their transport before they can pass traffic (e.g. olcrtc WebRTC).
    // awaitReady() waits for these to accept connections.
    private val readinessPorts = mutableListOf<Int>()
    open lateinit var processes: GuardedProcessPool
    fun processesOrNull() = if (::processes.isInitialized) processes else null
    private var cacheFiles = ArrayList<File>()
    fun isInitialized(): Boolean {
        return ::config.isInitialized
    }

    fun hasDnsTunnel() = isInitialized() && config.dnsttClients.isNotEmpty()

    fun hasJitsi() = isInitialized() && config.index.any { (_, chain) ->
        chain.values.any { (it.requireBean() as? OLCRTCBean)?.authProvider == "jitsi" }
    }

    protected open fun buildConfig() {
        config = buildV2RayConfig(profile)
    }

    protected open fun loadConfig() {
        v2rayPoint.loadConfig(config.config)
    }

    open suspend fun init() {
        v2rayPoint = V2RayInstance()
        buildConfig()
        if (config.dnsttClients.isNotEmpty()) {
            if (DataStore.serviceMode == Key.MODE_VPN && DataStore.tunImplementation == TunImplementation.SYSTEM) {
                error("DNS Tunnel requires gVisor TUN")
            }
            require(config.dnsttClients.size == 1) { "Only one DNS Tunnel is supported per connection" }
            dnsTunnelConfig = config.dnsttClients.single()
        }
        for ((_, chain) in config.index) {
            chain.entries.forEachIndexed { _, (triple, profile) ->
                val port = triple.first
                val username = triple.second
                val password = triple.third
                when (val bean = profile.requireBean()) {
                    is OLCRTCBean -> {
                        val yamlConfig = buildOlcrtcYaml(bean, port, username, password)
                        olcrtcConfigs[port] = yamlConfig
                    }
                    else -> error("Unsupported external profile type ${profile.type}")
                }
            }
        }
        loadConfig()
        config.config = ""
    }

    override fun launch() {
        val context = SagerNet.application
        for ((_, chain) in config.index) {
            chain.entries.forEachIndexed { _, (triple, profile) ->
                val port = triple.first
                val bean = profile.requireBean()
                val env = mutableMapOf<String, String>()
                if (DataStore.providerRootCA != RootCAProvider.SYSTEM) {
                    env["SSL_CERT_FILE"] = when (DataStore.providerRootCA) {
                        RootCAProvider.MOZILLA -> {
                            File(app.filesDir, "mozilla_included.pem").canonicalPath
                        }
                        RootCAProvider.SYSTEM_AND_USER -> {
                            File(app.filesDir, "android_included.pem").canonicalPath
                        }
                        RootCAProvider.CUSTOM -> {
                            File(app.externalAssets, "root_store.certs").canonicalPath
                        }
                        else -> error("impossible")
                    }
                }
                when (bean) {
                    is OLCRTCBean -> {
                        val yamlConfig = olcrtcConfigs.getValue(port)
                        val configFile = File(
                            context.noBackupFilesDir,
                            "olcrtc_" + SystemClock.elapsedRealtime() + ".yaml"
                        )
                        configFile.parentFile?.mkdirs()
                        configFile.writeText(yamlConfig)
                        cacheFiles.add(configFile)
                        val olcrtcBin = File(context.applicationInfo.nativeLibraryDir, "libolcrtc.so")
                        Logs.i("olcrtc: nativeLibraryDir=${context.applicationInfo.nativeLibraryDir}")
                        Logs.i("olcrtc: binary=${olcrtcBin.absolutePath} exists=${olcrtcBin.exists()} canExecute=${olcrtcBin.canExecute()} size=${if (olcrtcBin.exists()) olcrtcBin.length() else -1}")
                        Logs.i("olcrtc: config file=${configFile.absolutePath} exists=${configFile.exists()}")
                        if (!olcrtcBin.exists()) {
                            Logs.e("olcrtc: binary libolcrtc.so NOT FOUND in nativeLibraryDir. " +
                                "Check that jniLibs/<abi>/libolcrtc.so is packaged and extractNativeLibs/useLegacyPackaging is enabled.")
                        }
                        val commands = mutableListOf(
                            olcrtcBin.absolutePath,
                            configFile.absolutePath
                        )
                        Logs.i("olcrtc: launching: ${commands.joinToString(" ")}")
                        processes.start(commands, env)
                        // olcrtc needs to negotiate WebRTC before its local
                        // SOCKS listener is usable; awaitReady() waits on it.
                        readinessPorts.add(port)
                    }
                    else -> error("Unsupported external profile type ${profile.type}")
                }
            }
        }
        v2rayPoint.start()
    }

    /**
     * Wait until every external engine's local SOCKS port accepts a TCP
     * connection, so the service stays in the Connecting state until traffic
     * can really flow (olcrtc must finish WebRTC negotiation first).
     *
     * Times out after [READINESS_TIMEOUT_MS] and returns anyway, so a stuck
     * engine still surfaces to the user as connected-but-failing rather than
     * hanging the connect flow forever.
     */
    override suspend fun awaitReady() {
        dnsTunnelConfig?.let { client ->
            val resolvers = client.resolvers.ifEmpty { automaticDnsttResolvers(underlayDnsServers()) }
            val deadline = SystemClock.elapsedRealtime() + dnsTunnelReadyTimeoutMs
            var lastError: Throwable? = null
            for ((index, resolver) in resolvers.withIndex()) {
                val remaining = deadline - SystemClock.elapsedRealtime()
                if (remaining <= 0) break
                val instance = SlipstreamInstance(client, resolver, ::onDnsTunnelStopped)
                dnsTunnelInstance = instance
                try {
                    withTimeout((remaining / (resolvers.size - index).coerceAtLeast(1)).coerceAtLeast(1L)) {
                        instance.launch()
                        instance.awaitReady()
                    }
                    lastError = null
                    break
                } catch (error: TimeoutCancellationException) {
                    lastError = error
                } catch (error: CancellationException) {
                    instance.close()
                    dnsTunnelInstance = null
                    throw error
                } catch (error: Throwable) {
                    lastError = error
                }
                instance.close()
                if (instance.isRunning()) {
                    throw IOException("DNS Tunnel resolver process did not stop")
                }
                if (dnsTunnelInstance === instance) dnsTunnelInstance = null
            }
            if (lastError != null || dnsTunnelInstance == null) {
                dnsTunnelInstance?.close()
                dnsTunnelInstance = null
                throw IOException(
                    "DNS Tunnel resolver did not become ready within ${dnsTunnelReadyTimeoutMs / 1000} seconds",
                    lastError,
                )
            }
        }
        if (readinessPorts.isEmpty()) return
        withContext(Dispatchers.IO) {
            for (port in readinessPorts) {
                val deadline = SystemClock.elapsedRealtime() + READINESS_TIMEOUT_MS
                Logs.i("readiness: waiting for local SOCKS 127.0.0.1:$port to come up (timeout ${READINESS_TIMEOUT_MS}ms)")
                var ready = false
                while (SystemClock.elapsedRealtime() < deadline) {
                    if (!isActive) return@withContext
                    try {
                        Socket().use { sock ->
                            sock.connect(InetSocketAddress(LOCALHOST, port), READINESS_CONNECT_TIMEOUT_MS)
                        }
                        ready = true
                        break
                    } catch (_: Exception) {
                        delay(READINESS_POLL_INTERVAL_MS)
                    }
                }
                if (ready) {
                    Logs.i("readiness: local SOCKS 127.0.0.1:$port is up")
                } else {
                    Logs.w("readiness: local SOCKS 127.0.0.1:$port did not come up within ${READINESS_TIMEOUT_MS}ms; continuing anyway")
                }
            }
        }
    }

    var isClosed = false
        private set

    @Suppress("EXPERIMENTAL_API_USAGE")
    override fun close() {
        if (isClosed) return

        runCatching { dnsTunnelInstance?.close() }
        dnsTunnelInstance = null

        cacheFiles.removeAll { it.delete(); true }

        if (::processes.isInitialized) processes.close(GlobalScope + Dispatchers.IO)

        if (::v2rayPoint.isInitialized) {
            v2rayPoint.close()
        }

        isClosed = true
    }

}
