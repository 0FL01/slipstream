package io.nekohasekai.sagernet.bg.proto

import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class DnsMultipathAdapterTest {
    @Test
    fun separateEndpointsKeepResponseSourceAndCloseBlockedReceivers() {
        val loopback = InetAddress.getByName("127.0.0.1")
        val pool = Executors.newFixedThreadPool(2)
        val servers = List(2) { ServerSocket(0, 1, loopback) }
        val adapters = servers.map {
            SlipstreamInstance.DnsTcpAdapter("127.0.0.1", it.localPort, 1, 2) {
                System.nanoTime() / 1_000_000
            }
        }
        try {
            assertNotEquals(adapters[0].port, adapters[1].port)
            val tasks = servers.mapIndexed { index, server ->
                pool.submit {
                    server.accept().use { socket ->
                        socket.soTimeout = 3000
                        val input = DataInputStream(socket.getInputStream())
                        val output = DataOutputStream(socket.getOutputStream())
                        val data = ByteArray(input.readUnsignedShort())
                        input.readFully(data)
                        Thread.sleep(if (index == 0) 100 else 10)
                        output.writeShort(data.size)
                        output.write(data)
                        output.flush()
                        assertEquals(-1, input.read())
                    }
                }
            }
            DatagramSocket(InetSocketAddress(loopback, 0)).use { udp ->
                udp.soTimeout = 3000
                val packets = List(2) { index -> ByteArray(24) { (index * 32 + it).toByte() } }
                adapters.forEachIndexed { index, adapter ->
                    udp.send(DatagramPacket(packets[index], 24, loopback, adapter.port))
                }
                val seen = mutableSetOf<Int>()
                repeat(2) {
                    val packet = DatagramPacket(ByteArray(64), 64)
                    udp.receive(packet)
                    val index = adapters.indexOfFirst { it.port == packet.port }
                    assertTrue(index >= 0)
                    assertTrue(seen.add(index))
                    assertArrayEquals(packets[index], packet.data.copyOf(packet.length))
                }
            }
            adapters.forEach { it.close(); it.close() }
            tasks.forEach { it.get(3, TimeUnit.SECONDS) }
        } finally {
            adapters.forEach { it.close() }
            servers.forEach { it.close() }
            pool.shutdownNow()
            assertTrue(pool.awaitTermination(3, TimeUnit.SECONDS))
        }
    }
}
