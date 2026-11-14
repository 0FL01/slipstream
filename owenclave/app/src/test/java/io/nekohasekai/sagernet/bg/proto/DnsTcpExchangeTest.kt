package io.nekohasekai.sagernet.bg.proto

import org.junit.Assert.*
import org.junit.Test
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException
import java.io.FilterInputStream
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketAddress
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

class DnsTcpExchangeTest {
    private fun now() = System.nanoTime() / 1_000_000
    private val payload = ByteArray(12) { it.toByte() }

    private fun receive(socket: Socket) {
        socket.soTimeout = 2000
        val input = DataInputStream(socket.getInputStream())
        val bytes = ByteArray(input.readUnsignedShort())
        input.readFully(bytes)
        assertArrayEquals(payload, bytes)
    }

    private fun reply(socket: Socket) {
        DataOutputStream(socket.getOutputStream()).apply {
            writeShort(payload.size)
            write(payload)
            flush()
        }
    }

    private fun fixture(block: (ServerSocket, ScheduledThreadPoolExecutor) -> Unit) {
        ServerSocket(0, 56, java.net.InetAddress.getLoopbackAddress()).use { server ->
            server.soTimeout = 2000
            val deadlines = ScheduledThreadPoolExecutor(1).apply { removeOnCancelPolicy = true }
            try {
                block(server, deadlines)
            } finally {
                deadlines.shutdownNow()
                assertTrue(deadlines.awaitTermination(2, TimeUnit.SECONDS))
            }
        }
    }

    private fun exchange(server: ServerSocket, timer: ScheduledThreadPoolExecutor, age: Long = 300,
                         factory: () -> Socket = ::Socket) = DnsTcpExchange(
        InetSocketAddress(server.inetAddress, server.localPort), timer, ::now, age, 200, factory,
    )

    @Test fun legacyFragmentedReadExceedsWholeQueryAge() = fixture { server, _ ->
        val pool = Executors.newSingleThreadExecutor()
        try {
            val peer = pool.submit {
                server.accept().use { socket ->
                    receive(socket)
                    val output = socket.getOutputStream()
                    for (byte in byteArrayOf(0, 12) + payload) {
                        Thread.sleep(45)
                        output.write(byte.toInt())
                        output.flush()
                    }
                }
            }
            // Explicit specimen of the original blocking read sequence, not source extraction.
            Socket().use { socket ->
                val start = now()
                socket.connect(InetSocketAddress(server.inetAddress, server.localPort), 200)
                socket.soTimeout = 200
                DataOutputStream(socket.getOutputStream()).apply { writeShort(12); write(payload); flush() }
                val input = DataInputStream(socket.getInputStream())
                val response = ByteArray(input.readUnsignedShort())
                input.readFully(response)
                assertArrayEquals(payload, response)
                assertTrue("legacy delivered after 300ms age", now() - start > 500)
            }
            peer.get(2, TimeUnit.SECONDS)
        } finally { pool.shutdownNow() }
    }

    @Test fun fragmentedReplyExpiresAndNeverDelivers() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        try {
            val peer = pool.submit {
                server.accept().use { socket ->
                    receive(socket)
                    try {
                        for (byte in byteArrayOf(0, 12) + payload) {
                            Thread.sleep(45)
                            socket.getOutputStream().write(byte.toInt())
                        }
                    } catch (_: IOException) { }
                }
            }
            exchange(server, timer).use { worker ->
                val start = now()
                worker.query(payload, start) { fail("stale reply") }
                assertTrue(now() - start in 250..700)
                assertEquals(0, timer.queue.size)
            }
            peer.get(2, TimeUnit.SECONDS)
        } finally { pool.shutdownNow() }
    }

    @Test fun fastPathReusesConnectionAndLocalFailureDoesNotReplay() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        val requests = AtomicInteger()
        try {
            val peer = pool.submit {
                server.accept().use { socket ->
                    repeat(100) {
                        receive(socket)
                        requests.incrementAndGet()
                        reply(socket)
                    }
                }
            }
            exchange(server, timer, 2000).use { worker ->
                try {
                    worker.query(payload, now()) { throw IOException("local send failure") }
                    fail("delivery exception must not be swallowed or retried")
                } catch (_: IOException) { }
                repeat(99) {
                    worker.query(payload, now()) { assertArrayEquals(payload, it) }
                    assertEquals(0, timer.queue.size)
                }
            }
            peer.get(2, TimeUnit.SECONDS)
            assertEquals(100, requests.get())
        } finally { pool.shutdownNow() }
    }

    @Test fun expiredQueueItemDoesNotOpenSocket() = fixture { server, timer ->
        exchange(server, timer, factory = { error("expired query opened socket") }).use {
            it.query(payload, now() - 301) { fail("expired queue delivered") }
        }
        assertEquals(0, timer.queue.size)
    }

    @Test fun retrySucceedsOnceWithinOriginalBudget() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        try {
            val peer = pool.submit {
                server.accept().use { receive(it) }
                server.accept().use { receive(it); reply(it) }
            }
            var delivered = 0
            exchange(server, timer, 1000).use {
                it.query(payload, now()) { bytes -> assertArrayEquals(payload, bytes); delivered++ }
            }
            assertEquals(1, delivered)
            peer.get(2, TimeUnit.SECONDS)
        } finally { pool.shutdownNow() }
    }

    @Test fun retryDoesNotResetQueueAgeBudget() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        val attempts = AtomicInteger()
        try {
            val peer = pool.submit {
                repeat(2) { index ->
                    server.accept().use { socket ->
                        attempts.incrementAndGet()
                        receive(socket)
                        Thread.sleep(if (index == 0) 100 else 250)
                        if (index == 1) runCatching { reply(socket) }
                    }
                }
            }
            exchange(server, timer, 400).use {
                val start = now()
                it.query(payload, start - 150) { fail("retry delivered stale response") }
                assertTrue(now() - start in 200..600)
            }
            peer.get(2, TimeUnit.SECONDS)
            assertEquals(2, attempts.get())
        } finally { pool.shutdownNow() }
    }

    @Test fun invalidLengthRetriesAtMostOnce() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        val sockets = AtomicInteger()
        try {
            val peer = pool.submit {
                repeat(2) {
                    server.accept().use { socket ->
                        receive(socket)
                        DataOutputStream(socket.getOutputStream()).apply { writeShort(4097); flush() }
                    }
                }
            }
            exchange(server, timer, 1000) { sockets.incrementAndGet(); Socket() }.use {
                it.query(payload, now()) { fail("invalid response") }
            }
            peer.get(2, TimeUnit.SECONDS)
            assertEquals(2, sockets.get())
        } finally { pool.shutdownNow() }
    }

    @Test fun delayedConnectConsumesReadBudget() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        try {
            val peer = pool.submit {
                server.accept().use { socket ->
                    receive(socket)
                    Thread.sleep(180)
                    runCatching { reply(socket) }
                }
            }
            exchange(server, timer, 300) {
                object : Socket() {
                    override fun connect(endpoint: SocketAddress, timeout: Int) {
                        super.connect(endpoint, timeout)
                        Thread.sleep(180)
                    }
                }
            }.use {
                val start = now()
                it.query(payload, start) { fail("connect time was excluded") }
                assertTrue(now() - start in 250..650)
            }
            peer.get(2, TimeUnit.SECONDS)
        } finally { pool.shutdownNow() }
    }

    @Test fun finalAgeCheckRejectsReplyEvenBeforeTimerRuns() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        val clock = AtomicLong(1000)
        val sockets = AtomicInteger()
        try {
            val peer = pool.submit { server.accept().use { receive(it); reply(it) } }
            DnsTcpExchange(InetSocketAddress(server.inetAddress, server.localPort), timer,
                clock::get, 300, 200) {
                sockets.incrementAndGet()
                object : Socket() {
                    override fun getInputStream(): InputStream = object : FilterInputStream(super.getInputStream()) {
                        override fun read(bytes: ByteArray, offset: Int, length: Int): Int {
                            val count = super.read(bytes, offset, length)
                            clock.set(1400)
                            return count
                        }
                    }
                }
            }.use { it.query(payload, 1000) { fail("stale response delivered before timer") } }
            peer.get(2, TimeUnit.SECONDS)
            assertEquals(1, sockets.get())
            assertEquals(0, timer.queue.size)
        } finally { pool.shutdownNow() }
    }

    // Deterministic socket syscall stalls: close must release connect AND flush, not
    // just reads. A real loopback peer cannot reliably force connect/write backpressure.
    private class StalledSocket(private val phase: String) : Socket() {
        val entered = CountDownLatch(1)
        private val stopped = CountDownLatch(1)
        private fun stall(): Nothing {
            entered.countDown()
            check(stopped.await(2, TimeUnit.SECONDS)) { "socket was not closed" }
            throw IOException("closed")
        }
        override fun connect(endpoint: SocketAddress, timeout: Int) {
            if (phase == "connect") stall()
            super.connect(endpoint, timeout)
        }
        override fun getOutputStream(): OutputStream = object : OutputStream() {
            override fun write(value: Int) { stall() }
            override fun write(bytes: ByteArray, offset: Int, length: Int) { stall() }
        }
        override fun close() { stopped.countDown(); super.close() }
    }

    @Test fun deadlineClosesStalledConnectAndWrite() = fixture { server, timer ->
        for (phase in listOf("connect", "write")) {
            val socket = StalledSocket(phase)
            exchange(server, timer, factory = { socket }).use {
                val start = now()
                it.query(payload, start) { fail("stalled query delivered") }
                assertEquals(0L, socket.entered.count)
                assertTrue(socket.isClosed)
                assertTrue(now() - start in 250..700)
            }
        }
    }

    @Test fun closeCancelsStalledConnectWriteAndRead() = fixture { server, timer ->
        val pool = Executors.newSingleThreadExecutor()
        try {
            for (phase in listOf("connect", "write", "read")) {
                val socket = if (phase == "read") Socket() else StalledSocket(phase)
                val worker = exchange(server, timer, 10_000) { socket }
                val work = pool.submit { worker.query(payload, now()) { fail("closed delivered") } }
                var readPeer: Socket? = null
                if (socket is StalledSocket) assertTrue(socket.entered.await(1, TimeUnit.SECONDS))
                else {
                    // Previous write-phase connection may still be in the accept backlog.
                    var peer = server.accept()
                    while (peer.port != socket.localPort) { peer.close(); peer = server.accept() }
                    receive(peer)
                    readPeer = peer
                }
                val start = now()
                worker.close()
                work.get(1, TimeUnit.SECONDS)
                readPeer?.close()
                assertTrue(now() - start < 500)
                assertEquals(0, timer.queue.size)
            }
        } finally { pool.shutdownNow() }
    }

    @Test fun saturatedWorkersHaveBoundedTimersAndIndependentExpiry() = fixture { server, timer ->
        val pool = Executors.newFixedThreadPool(56)
        val sockets = List(56) { StalledSocket("connect") }
        val workers = sockets.map { socket -> exchange(server, timer, 1500) { socket } }
        try {
            val jobs = workers.map { worker -> pool.submit { worker.query(payload, now()) { fail() } } }
            sockets.forEach { assertTrue(it.entered.await(1, TimeUnit.SECONDS)) }
            assertTrue(timer.queue.size <= 56)
            assertEquals(1, timer.poolSize)
            jobs.forEach { it.get(3, TimeUnit.SECONDS) }
            assertTrue(sockets.all { it.isClosed })
            assertEquals(0, timer.queue.size)
        } finally {
            workers.forEach { it.close() }
            pool.shutdownNow()
            assertTrue(pool.awaitTermination(2, TimeUnit.SECONDS))
        }
    }
}
