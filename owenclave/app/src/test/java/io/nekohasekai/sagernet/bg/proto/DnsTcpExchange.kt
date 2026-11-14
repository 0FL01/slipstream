package io.nekohasekai.sagernet.bg.proto

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

/** One serial worker; the adapter owns the shared, bounded deadline executor. */
internal class DnsTcpExchange(
    private val address: InetSocketAddress,
    private val deadlines: ScheduledThreadPoolExecutor,
    private val now: () -> Long,
    private val maxAgeMs: Long = 10_000,
    private val socketTimeoutMs: Int = 8_000,
    private val newSocket: () -> Socket = ::Socket,
) : AutoCloseable {
    private val lock = Any()
    private var closed = false
    private var socket: Socket? = null
    private var input: DataInputStream? = null
    private var output: DataOutputStream? = null

    private fun disconnectLocked() {
        runCatching { socket?.close() }
        socket = null
        input = null
        output = null
    }

    fun query(data: ByteArray, createdAt: Long, deliver: (ByteArray) -> Unit) {
        val deadline = createdAt + maxAgeMs
        var expired = false
        var finished = false
        val timer = synchronized(lock) {
            if (closed || now() >= deadline) return
            deadlines.schedule({
                synchronized(lock) {
                    // A canceled task may already be running: do not close the next query.
                    if (!finished) {
                        expired = true
                        disconnectLocked()
                    }
                }
            }, deadline - now(), TimeUnit.MILLISECONDS)
        }
        fun remaining(): Int = synchronized(lock) {
            val left = deadline - now()
            if (closed || expired || left <= 0) throw SocketTimeoutException("DNS query expired")
            left.coerceAtMost(socketTimeoutMs.toLong()).toInt()
        }
        try {
            var response: ByteArray? = null
            for (attempt in 0..1) {
                try {
                    val current = synchronized(lock) {
                        remaining()
                        socket ?: newSocket().also { socket = it }
                    }
                    if (!current.isConnected) {
                        current.tcpNoDelay = true
                        current.connect(address, remaining())
                        val reader = DataInputStream(BufferedInputStream(current.getInputStream()))
                        val writer = DataOutputStream(BufferedOutputStream(current.getOutputStream()))
                        synchronized(lock) {
                            remaining()
                            input = reader
                            output = writer
                        }
                    }
                    val (reader, writer) = synchronized(lock) {
                        remaining()
                        input!! to output!!
                    }
                    current.soTimeout = remaining()
                    writer.writeShort(data.size)
                    writer.write(data)
                    writer.flush()
                    current.soTimeout = remaining()
                    val length = reader.readUnsignedShort()
                    if (length !in 12..4096) throw IOException("Invalid DNS response")
                    val bytes = ByteArray(length)
                    reader.readFully(bytes)
                    remaining()
                    response = bytes
                    break
                } catch (_: IOException) {
                    synchronized(lock) { disconnectLocked() }
                    if (attempt == 1) return
                    try {
                        remaining()
                    } catch (_: IOException) {
                        return
                    }
                }
            }
            if (response != null) {
                try {
                    remaining()
                } catch (_: IOException) {
                    return
                }
                // Delivery is not a resolver exchange and must never trigger its retry.
                deliver(response)
            }
        } finally {
            synchronized(lock) {
                finished = true
                timer.cancel(false)
            }
        }
    }

    override fun close() = synchronized(lock) {
        closed = true
        disconnectLocked()
    }
}
