"""Concurrent reply-ownership regressions; synthetic DNS headers only."""
import asyncio
import socket
import struct
import unittest

from upload_first import Observer, TCPModel


class ReverseOrigin(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport
        self.requests = []

    def datagram_received(self, data, peer):
        self.requests.append((data, peer))
        if len(self.requests) == 2:
            for query, target in reversed(self.requests):
                response = query[:2] + b'\x80\x00' + query[4:]
                self.transport.sendto(response, target)


class ReplyOwnership(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.loop = asyncio.get_running_loop()
        self.origin_transport, self.origin = await self.loop.create_datagram_endpoint(
            ReverseOrigin, local_addr=('127.0.0.1', 0))
        self.target = self.origin_transport.get_extra_info('sockname')
        self.queries = [struct.pack('!H', ident) + b'\x00' * 10 for ident in (11, 22)]

    async def asyncTearDown(self):
        self.origin_transport.close()

    async def test_observer_two_peers_reverse_replies(self):
        observer = Observer(self.target)
        transport, _ = await self.loop.create_datagram_endpoint(
            lambda: observer, local_addr=('127.0.0.1', 0))
        clients = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        try:
            for client, query in zip(clients, self.queries):
                client.bind(('127.0.0.1', 0))
                client.setblocking(False)
                await self.loop.sock_sendto(client, query, transport.get_extra_info('sockname'))
            replies = await asyncio.wait_for(asyncio.gather(
                *(self.loop.sock_recv(client, 4096) for client in clients)), 2)
            self.assertEqual([reply[:2] for reply in replies], [q[:2] for q in self.queries])
            self.assertEqual(observer.stats['replies'], 2)
            self.assertEqual(observer.pending, {})
        finally:
            transport.close()
            for client in clients:
                client.close()

    async def test_tcp_workers_two_peers_reverse_replies(self):
        counters = Observer(self.target)
        model = TCPModel(self.target, .002, counters)
        server = await asyncio.start_server(model.handle, '127.0.0.1', 0)
        writers = []
        try:
            connections = [await asyncio.open_connection(*server.sockets[0].getsockname()) for _ in range(2)]
            writers = [writer for _, writer in connections]
            for (_, writer), query in zip(connections, self.queries):
                writer.write(struct.pack('!H', len(query)) + query)
                await writer.drain()
            async def receive(reader):
                size = struct.unpack('!H', await reader.readexactly(2))[0]
                return await reader.readexactly(size)
            replies = await asyncio.wait_for(asyncio.gather(*(receive(r) for r, _ in connections)), 2)
            self.assertEqual([reply[:2] for reply in replies], [q[:2] for q in self.queries])
            self.assertEqual(counters.stats['replies'], 2)
            self.assertEqual(counters.stats['invalid_reply'], 0)
            self.assertEqual(len({peer for _, peer in self.origin.requests}), 2)
        finally:
            for writer in writers:
                writer.close()
                await writer.wait_closed()
            server.close()
            await server.wait_closed()
            tasks = list(model.tasks)
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)


if __name__ == '__main__':
    unittest.main()
