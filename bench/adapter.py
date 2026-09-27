"""TCP DNS adapter derived from Owenclave smoke/device.py (read-only source).

Serial persistent exchange per worker; drop-new queue, strict ID/QR/length,
10-second query age, up to 8 seconds per attempt, exactly one retry.
Only aggregate counters are exposed. DNS packets never leave memory as logs.
"""
import asyncio
import struct
import time
from backend import close_writer


class Adapter(asyncio.DatagramProtocol):
    def __init__(self, host, port=53, workers=56, capacity=64, timeout=8., age=10.):
        self.host, self.port = host, port
        self.workers, self.timeout, self.age = workers, timeout, age
        self.queue = asyncio.Queue(capacity)
        self.tasks, self.transport, self.closed = [], None, False
        self.stats = dict(received=0, replies=0, dropped=0, errors=0, expired=0,
                          queue_max=0, active=0, active_max=0, queue_s=0., service_s=0.,
                          query_bytes=0, reply_bytes=0, tcp_connects=0, retries=0)

    def connection_made(self, transport):
        self.transport = transport
        self.tasks = [asyncio.create_task(self.worker()) for _ in range(self.workers)]

    def datagram_received(self, data, peer):
        if self.closed:
            return
        self.stats['received'] += 1
        self.stats['query_bytes'] += len(data)
        if peer[0] != '127.0.0.1' or not 12 <= len(data) <= 4096:
            self.stats['dropped'] += 1
            return
        try:
            self.queue.put_nowait((data, peer, time.monotonic()))
            self.stats['queue_max'] = max(self.stats['queue_max'], self.queue.qsize())
        except asyncio.QueueFull:
            self.stats['dropped'] += 1

    async def worker(self):
        reader = writer = None
        try:
            while True:
                data, peer, entered = await self.queue.get()
                start = time.monotonic()
                self.stats['queue_s'] += start - entered
                self.stats['active'] += 1
                self.stats['active_max'] = max(self.stats['active_max'], self.stats['active'])
                try:
                    for attempt in range(2):
                        remaining = min(self.timeout, self.age - (time.monotonic() - entered))
                        if remaining <= 0:
                            self.stats['expired'] += 1
                            break
                        if attempt:
                            self.stats['retries'] += 1
                        try:
                            async with asyncio.timeout(remaining):
                                if writer is None:
                                    reader, writer = await asyncio.open_connection(self.host, self.port, limit=8192)
                                    self.stats['tcp_connects'] += 1
                                writer.write(struct.pack('!H', len(data)) + data)
                                await writer.drain()
                                size = struct.unpack('!H', await reader.readexactly(2))[0]
                                if not 12 <= size <= 4096:
                                    raise ValueError('DNS length')
                                reply = await reader.readexactly(size)
                                if reply[:2] != data[:2] or not reply[2] & 0x80:
                                    raise ValueError('DNS identity')
                                self.transport.sendto(reply, peer)
                                self.stats['replies'] += 1
                                self.stats['reply_bytes'] += len(reply)
                                break
                        except (OSError, ValueError, EOFError, TimeoutError):
                            self.stats['errors'] += 1
                            if writer:
                                await close_writer(writer)
                            reader = writer = None
                finally:
                    self.stats['active'] -= 1
                    self.stats['service_s'] += time.monotonic() - start
                    self.queue.task_done()
        finally:
            if writer:
                await close_writer(writer)

    async def close(self):
        self.closed = True
        if self.transport:
            self.transport.close()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        while not self.queue.empty():
            self.queue.get_nowait()
            self.queue.task_done()
