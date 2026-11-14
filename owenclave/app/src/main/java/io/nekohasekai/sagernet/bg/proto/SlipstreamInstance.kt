package io.nekohasekai.sagernet.bg.proto

import android.os.Build
import android.os.SystemClock
import io.nekohasekai.sagernet.R
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.bg.AbstractInstance
import io.nekohasekai.sagernet.fmt.DnsttClientConfig
import io.nekohasekai.sagernet.fmt.dnstt.DnsttResolver
import io.nekohasekai.sagernet.fmt.dnstt.decodeDnsttToken
import io.nekohasekai.sagernet.ktx.Logs
import io.nekohasekai.sagernet.ktx.joinHostPort
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketException
import java.util.Collections
import java.util.concurrent.atomic.AtomicBoolean

internal class SlipstreamInstance(
    private val config: DnsttClientConfig,
    private val resolver: DnsttResolver,
    private val onStopped: (IOException) -> Unit,
) : AbstractInstance {
    private companion object {
        const val WORKERS = 56
        const val QUERY_QUEUE_CAPACITY = 64
        const val QUERY_MAX_AGE_MS = 10_000L
        const val SOCKET_TIMEOUT_MS = 8_000
    }

    internal class DnsTcpAdapter(
        private val resolverHost: String,
        private val resolverPort: Int,
        workers: Int = WORKERS,
        queueCapacity: Int = QUERY_QUEUE_CAPACITY,
        private val elapsedRealtime: () -> Long = SystemClock::elapsedRealtime,
    ) : AutoCloseable {
        private data class Query(
            val data: ByteArray,
            val source: InetSocketAddress,
            val createdAt: Long,
        )

        private data class Connection(
            val socket: Socket,
            val input: DataInputStream,
            val output: DataOutputStream,
        ) : AutoCloseable {
            override fun close() = socket.close()
        }

        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        private val queue = Channel<Query>(queueCapacity)
        private val udp = DatagramSocket(null).apply {
            bind(InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0))
        }
        private val connections = Collections.synchronizedSet(mutableSetOf<Socket>())
        private val closed = AtomicBoolean()

        val port: Int get() = udp.localPort

        init {
            scope.launch {
                while (isActive) {
                    val buffer = ByteArray(4096)
                    val packet = DatagramPacket(buffer, buffer.size)
                    try {
                        udp.receive(packet)
                    } catch (_: SocketException) {
                        break
                    }
                    queue.trySend(
                        Query(
                            packet.data.copyOfRange(packet.offset, packet.offset + packet.length),
                            packet.socketAddress as InetSocketAddress,
                            elapsedRealtime(),
                        ),
                    )
                }
            }
            repeat(workers) { scope.launch { worker() } }
        }

        private fun connect(timeoutMs: Int): Connection {
            val socket = Socket()
            synchronized(connections) {
                if (closed.get()) {
                    socket.close()
                    throw IOException("DNS TCP adapter is closed")
                }
                connections.add(socket)
            }
            try {
                socket.tcpNoDelay = true
                socket.connect(InetSocketAddress(resolverHost, resolverPort), timeoutMs)
                socket.soTimeout = timeoutMs
                return Connection(
                    socket,
                    DataInputStream(BufferedInputStream(socket.getInputStream())),
                    DataOutputStream(BufferedOutputStream(socket.getOutputStream())),
                )
            } catch (error: Throwable) {
                connections.remove(socket)
                runCatching { socket.close() }
                throw error
            }
        }

        private fun closeConnection(connection: Connection?) {
            connection ?: return
            connections.remove(connection.socket)
            runCatching { connection.close() }
        }

        private fun remainingMs(query: Query) =
            (QUERY_MAX_AGE_MS - (elapsedRealtime() - query.createdAt))
                .coerceAtMost(SOCKET_TIMEOUT_MS.toLong()).toInt()

        private suspend fun worker() {
            var connection: Connection? = null
            try {
                for (query in queue) {
                    if (closed.get() || !currentCoroutineContext().isActive || remainingMs(query) <= 0) continue
                    for (attempt in 0..1) {
                        try {
                            val remaining = remainingMs(query)
                            if (remaining <= 0) break
                            val current = connection ?: connect(remaining).also { connection = it }
                            current.socket.soTimeout = remaining
                            current.output.writeShort(query.data.size)
                            current.output.write(query.data)
                            current.output.flush()
                            val responseLength = current.input.readUnsignedShort()
                            if (responseLength !in 12..4096) throw IOException("Invalid DNS response")
                            val response = ByteArray(responseLength)
                            current.input.readFully(response)
                            udp.send(DatagramPacket(response, response.size, query.source))
                            break
                        } catch (_: IOException) {
                            closeConnection(connection)
                            connection = null
                            if (
                                attempt == 1 || closed.get() ||
                                !currentCoroutineContext().isActive || remainingMs(query) <= 0
                            ) break
                        }
                    }
                }
            } finally {
                closeConnection(connection)
            }
        }

        override fun close() {
            if (!closed.compareAndSet(false, true)) return
            scope.cancel()
            queue.close()
            udp.close()
            val sockets = synchronized(connections) {
                connections.toList().also { connections.clear() }
            }
            sockets.forEach { runCatching { it.close() } }
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val lock = Any()
    private val adapters = mutableListOf<DnsTcpAdapter>()
    private lateinit var authorities: List<String>
    private val ready = CompletableDeferred<Unit>()
    private var process: Process? = null
    private var readyAccepted = false
    private lateinit var certificate: File
    @Volatile
    private var closed = false

    private fun drain(stream: InputStream) {
        scope.launch {
            runCatching {
                stream.bufferedReader().useLines { lines ->
                    lines.forEach { if (it.contains("Connection ready")) ready.complete(Unit) }
                }
            }
        }
    }

    private fun isAlive(child: Process?): Boolean {
        if (child == null) return false
        return try {
            child.exitValue()
            false
        } catch (_: IllegalThreadStateException) {
            true
        }
    }

    private fun stopLocked() {
        val child = process ?: return
        child.destroy()
        var attempts = 0
        while (isAlive(child) && attempts < 10) {
            Thread.sleep(50)
            attempts++
        }
        if (isAlive(child) && Build.VERSION.SDK_INT >= 26) {
            child.destroyForcibly()
            attempts = 0
            while (isAlive(child) && attempts < 10) {
                Thread.sleep(50)
                attempts++
            }
        }
        process = child.takeIf(::isAlive)
    }

    fun isRunning() = synchronized(lock) { isAlive(process) }

    private fun startLocked() {
        val binary = File(SagerNet.application.applicationInfo.nativeLibraryDir, "libslipstream.so")
        if (!binary.canExecute()) throw IOException("Slipstream binary is unavailable")
        val command = listOf(
            binary.absolutePath,
            "--tcp-listen-host", "127.0.0.1",
            "--tcp-listen-port", config.localPort.toString(),
            "--domain", config.domain,
            "--cert", certificate.absolutePath,
            "--congestion-control", "dcubic",
            "--flow-relay-stdin",
        ) + authorities.flatMap { listOf("--authoritative", it) }
        val child = ProcessBuilder(command)
            .directory(SagerNet.application.noBackupFilesDir)
            .start()
        try {
            child.outputStream.use { output ->
                output.write(decodeDnsttToken(config.token))
                output.write(config.socksUsername.toByteArray(Charsets.US_ASCII))
                output.write(config.socksPassword.toByteArray(Charsets.US_ASCII))
            }
        } catch (error: Throwable) {
            child.destroy()
            throw IOException("Could not bootstrap FlowRelay", error)
        }
        process = child
        drain(child.inputStream)
        drain(child.errorStream)
        scope.launch {
            child.waitFor()
            val stoppedAfterReadiness = synchronized(lock) {
                if (closed) {
                    false
                } else if (readyAccepted) {
                    true
                } else {
                    ready.completeExceptionally(IOException("Slipstream stopped before becoming ready"))
                    false
                }
            }
            if (stoppedAfterReadiness) {
                Logs.w("slipstream: process stopped after readiness")
                onStopped(IOException("DNS Tunnel carrier stopped"))
            }
        }
        Logs.i("slipstream: starting DNS carrier")
    }

    override fun launch() {
        synchronized(lock) {
            check(!closed)
            certificate = File(SagerNet.application.noBackupFilesDir, "slipstream-server.crt")
            SagerNet.application.resources.openRawResource(R.raw.slipstream_server).use { input ->
                certificate.outputStream().use { input.copyTo(it) }
            }
            try {
                val paths = resolver.paths()
                authorities = paths.map { path ->
                    when (path.transport) {
                        "udp" -> joinHostPort(path.host, path.port)
                        "tcp" -> DnsTcpAdapter(path.host, path.port, WORKERS / paths.size, QUERY_QUEUE_CAPACITY / paths.size).let {
                            adapters.add(it)
                            "127.0.0.1:${it.port}"
                        }
                        else -> throw IOException("Unsupported DNS resolver transport")
                    }
                }
                startLocked()
            } catch (error: Throwable) {
                adapters.forEach { runCatching { it.close() } }
                adapters.clear()
                certificate.delete()
                throw error
            }
        }
    }

    override suspend fun awaitReady() {
        withContext(Dispatchers.IO) { ready.await() }
        synchronized(lock) {
            if (closed) throw IOException("Slipstream closed before becoming ready")
            if (!isAlive(process)) throw IOException("Slipstream stopped before becoming ready")
            readyAccepted = true
        }
        Logs.i("slipstream: carrier ready")
    }

    override fun close() {
        synchronized(lock) {
            if (closed) return
            closed = true
            ready.cancel()
            stopLocked()
            adapters.forEach { runCatching { it.close() } }
            adapters.clear()
            if (::certificate.isInitialized) certificate.delete()
        }
        scope.cancel()
    }
}
