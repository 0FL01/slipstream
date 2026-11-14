"""Loopback-only Slipstream/HTTPS causal probe; not Android performance acceptance.

One fixed authenticated bridge, two bounded synthetic DNS paths, no traffic output.
All child output is discarded. Certificates/credentials are ephemeral.
"""
import argparse
import asyncio
import contextlib
import hashlib
import hmac
import json
import os
from pathlib import Path
import signal
import socket
import ssl
import struct
import subprocess
import tempfile
from types import SimpleNamespace

import device
from fixture import Fixture


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class PathAdapter(device.Adapter):
    """Synthetic recursive service latency; never inspects or retains DNS contents."""
    def __init__(self, port, delay, workers=28, capacity=32):
        super().__init__('127.0.0.1', port, workers=workers, capacity=capacity)
        self.delay = delay

    async def worker(self, index):
        loop = asyncio.get_running_loop()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setblocking(False)
            sock.connect(('127.0.0.1', self.port))
            while True:
                data, peer, entered = await self.queue.get()
                start = loop.time()
                self.stats['active'] += 1
                self.stats['active_max'] = max(self.stats['active_max'], self.stats['active'])
                self.stats['queue_s'] += start - entered
                try:
                    async with asyncio.timeout(2):
                        await loop.sock_sendall(sock, data)
                        while True:
                            reply = await loop.sock_recv(sock, 4096)
                            if reply[:2] == data[:2]:
                                break
                        await asyncio.sleep(max(0, self.delay - (loop.time() - start)))
                        self.transport.sendto(reply, peer)
                        self.stats['replies'] += 1
                        self.stats['reply_bytes'] += len(reply)
                except (OSError, TimeoutError):
                    self.stats['errors'] += 1
                finally:
                    self.stats['active'] -= 1
                    self.stats['service_s'] += loop.time() - start
                    self.queue.task_done()


async def window(args, root, index):
    token, fixture_token = os.urandom(16), os.urandom(32).hex()
    stats = dict(bridge_authenticated=0, bridge_up=0, bridge_down=0,
                 tls_complete=0, headers_complete=0, body_complete=0, reply_drained=0)
    tasks, writers, procs, paths = set(), set(), [], []
    servers, drains = [], []
    fixture = Fixture(fixture_token)
    class Reader:
        def __init__(self, reader):
            self.reader = reader

        async def readuntil(self, separator):
            value = await self.reader.readuntil(separator)
            stats['headers_complete'] += 1
            return value

        async def readexactly(self, size):
            value = await self.reader.readexactly(size)
            stats['body_complete'] += 1
            return value

    class Writer:
        def __init__(self, writer):
            self.writer = writer

        def __getattr__(self, key):
            return getattr(self.writer, key)

        async def drain(self):
            await self.writer.drain()
            stats['reply_drained'] += 1

    async def sink(reader, writer):
        stats['tls_complete'] += 1
        await fixture.handle(Reader(reader), Writer(writer))

    async def bridge(reader, writer):
        task = asyncio.current_task()
        if len(tasks) >= 8:
            await device.close_writer(writer)
            return
        tasks.add(task)
        writers.add(writer)
        target = None
        try:
            async with asyncio.timeout(5):
                expected = b'\x24' + token + b'\x09127.0.0.1' + struct.pack('!H', fixture_port)
                header = await reader.readexactly(len(expected))
                if not hmac.compare_digest(header, expected):
                    return
                stats['bridge_authenticated'] += 1
                upstream, target = await asyncio.open_connection('127.0.0.1', fixture_port)
                writers.add(target)
            async def copy(source, dest, name):
                while data := await source.read(16384):
                    dest.write(data)
                    await dest.drain()
                    stats[name] += len(data)
                dest.write_eof()
            async with asyncio.TaskGroup() as group:
                group.create_task(copy(reader, target, 'bridge_up'))
                group.create_task(copy(upstream, writer, 'bridge_down'))
        except (OSError, EOFError, TimeoutError, ExceptionGroup):
            pass
        finally:
            for stream in (writer, target):
                await device.close_writer(stream)
                writers.discard(stream)
            tasks.discard(task)

    try:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(root / 'cert.pem', root / 'key.pem')
        sink_server = await asyncio.start_server(sink, '127.0.0.1', 0, ssl=context,
                                                ssl_handshake_timeout=5, limit=8192, backlog=8)
        servers.append(sink_server)
        fixture_port = sink_server.sockets[0].getsockname()[1]
        relay = await asyncio.start_server(bridge, '127.0.0.1', 0)
        servers.append(relay)
        dnsport = free_port()
        server = await asyncio.create_subprocess_exec(str(args.server.resolve()),
            '--dns-listen-host', '127.0.0.1', '--dns-listen-port', str(dnsport),
            '--target-address', '127.0.0.1:' + str(relay.sockets[0].getsockname()[1]),
            '--domain', 'test.com', '--cert', str(root / 'cert.pem'), '--key', str(root / 'key.pem'),
            stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        procs.append(server)
        layouts = []
        partitions = args.connections if args.partition else 1
        for _ in range(partitions):
            argv = []
            for delay in args.delay:
                path = PathAdapter(dnsport, delay, 28 // partitions, 32 // partitions)
                transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                    lambda: path, local_addr=('127.0.0.1', 0))
                paths.append(path)
                argv += ['--authoritative', '127.0.0.1:' + str(transport.get_extra_info('sockname')[1])]
            layouts.append(argv)
        proxies = []
        async with asyncio.timeout(15):
            for connection in range(args.connections):
                proxies.append(await device.start_client(args.client.resolve(), 'test.com', root / 'cert.pem',
                    layouts[connection % partitions], token, procs, drains, args.cc))
        settings = SimpleNamespace(workload=args.workload, fixture_host='127.0.0.1', fixture_port=fixture_port,
                                   fixture_cert=str(root / 'cert.pem'), deadline=40, egress='unchecked')
        proxy = proxies[0]
        secret = {'fixture_token': fixture_token}
        if args.focused:
            started = asyncio.get_running_loop().time()
            async def load():
                while True:
                    await device.request(settings, secret, proxies[-1], 'down', 1048576, 'background')
            load_task = asyncio.create_task(load())
            async def observe():
                for sample in range(1, 9):
                    await asyncio.sleep(5)
                    device.emit(dict(kind='progress', index=index, sample=sample,
                                     seconds=asyncio.get_running_loop().time() - started,
                                     stats=dict(stats)))
            async def release():
                await asyncio.sleep(args.release_load_after)
                load_task.cancel()
                await asyncio.gather(load_task, return_exceptions=True)
                device.emit(dict(kind='load_released', index=index,
                                 seconds=asyncio.get_running_loop().time() - started))
            owned = [load_task, asyncio.create_task(observe())]
            if args.release_load_after:
                owned.append(asyncio.create_task(release()))
            try:
                await asyncio.sleep(.25)
                rows = [await device.request(settings, secret, proxy, 'up', 524288, 'loaded')]
            finally:
                for task in owned:
                    task.cancel()
                await asyncio.gather(*owned, return_exceptions=True)
            rows.append(await device.request(settings, secret, proxy, 'down', 4096, 'recovery'))
        else:
            rows = await device.workload(settings, secret, proxy, proxies[-1])
        device.emit(dict(kind='window', index=index, ok=all(r['ok'] for r in rows), stats=stats))
    finally:
        for proc in reversed(procs):
            if proc.returncode is None:
                proc.terminate()
                try:
                    await asyncio.wait_for(proc.wait(), 2)
                except TimeoutError:
                    proc.kill()
                    await proc.wait()
        for path in paths:
            await path.close()
        for server in servers:
            server.close()
            await server.wait_closed()
        pending = list(tasks | fixture.active | set(drains))
        for task in pending:
            task.cancel()
        await asyncio.gather(*pending, return_exceptions=True)
        for writer in list(writers):
            # A cancelled handler may already have cancelled its close waiter.
            writer.transport.abort()
        device.emit(dict(kind='cleanup', index=index, child_alive=any(p.returncode is None for p in procs),
                         tasks_alive=sum(not t.done() for t in pending), paths=[p.stats for p in paths]))


async def main(args):
    device.emit(dict(kind='manifest', scope='loopback synthetic; no Android/FlowRelay/WARP', delay=args.delay,
                     focused=args.focused, release_load_after=args.release_load_after,
                     connections=args.connections, partition=args.partition, workload=args.workload, cc=args.cc, workers=56, queue=64,
                     client_sha256=hashlib.sha256(args.client.read_bytes()).hexdigest(),
                     server_sha256=hashlib.sha256(args.server.read_bytes()).hexdigest()))
    loop = asyncio.get_running_loop()
    loop.add_signal_handler(signal.SIGTERM, asyncio.current_task().cancel)
    try:
        with tempfile.TemporaryDirectory(prefix='owenclave-causal-') as tmp:
            root = Path(tmp)
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1',
                            '-nodes', '-days', '1', '-subj', '/CN=causal', '-addext', 'subjectAltName=IP:127.0.0.1',
                            '-keyout', str(root / 'key.pem'), '-out', str(root / 'cert.pem')],
                           check=True, capture_output=True)
            for index in range(1, args.windows + 1):
                await window(args, root, index)
    finally:
        loop.remove_signal_handler(signal.SIGTERM)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--delay', type=float, nargs=2, default=[.08, .12])
    parser.add_argument('--windows', type=int, default=2)
    parser.add_argument('--focused', action='store_true', help='Only loaded 512KiB plus recovery; aggregate progress')
    parser.add_argument('--connections', type=int, choices=[1, 2], default=1)
    parser.add_argument('--cc', choices=['dcubic', 'bbr'], default='dcubic')
    parser.add_argument('--partition', action='store_true', help='Partition same 56/64 DNS budget by connection')
    parser.add_argument('--workload', choices=['full', 'short-parallel'], default='full')
    parser.add_argument('--release-load-after', type=float, default=0,
                        help='Focused causal intervention: stop DL at this second; zero keeps it present')
    args = parser.parse_args()
    if not 1 <= args.windows <= 6 or any(not 0 <= d <= .5 for d in args.delay):
        parser.error('bounded windows/delay required')
    if not 0 <= args.release_load_after <= 30 or (args.release_load_after and not args.focused):
        parser.error('load release requires focused mode and 0..30 seconds')
    if (args.partition and args.connections != 2) or (args.focused and args.workload != 'full'):
        parser.error('partition requires two connections; focused requires full workload')
    with args.output.open('x') as output, contextlib.redirect_stdout(output):
        asyncio.run(main(args))
