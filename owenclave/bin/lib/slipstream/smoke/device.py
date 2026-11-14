"""Foreground Android native screen or identical workload through an active gVisor VPN.

No third-party modules, traffic logs, persistent credentials or application data.
Python adapters deliberately are NOT an implementation-equivalent Kotlin benchmark.
"""
import argparse
import asyncio
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import platform
import re
import signal
import socket
import ssl
import struct
import sys
import time

VERSION = 2
SIZES = (8192, 32768, 131072, 524288)
TOPOLOGIES = {
    'accepted': [(('77.88.8.88',), 28, 32), (('77.88.8.1',), 28, 32)],
    'third': [(('77.88.8.88',), 19, 22), (('77.88.8.1',), 19, 21), (('77.88.8.8',), 18, 21)],
    'shard': [(('77.88.8.88',), 28, 32), (('77.88.8.1', '77.88.8.8'), 28, 32)],
}


def emit(row):
    print(json.dumps(row, allow_nan=False), flush=True)


async def close_writer(writer):
    if writer is not None:
        writer.close()
        try:
            await asyncio.wait_for(writer.wait_closed(), 1)
        except (OSError, TimeoutError):
            writer.transport.abort()


class Adapter(asyncio.DatagramProtocol):
    """One serial persistent TCP exchange per worker, drop-new bounded queue.

    Like Kotlin: age10s, per-attempt timeout min(8s, remaining age), one retry.
    Unlike Kotlin: async whole exchange timeout, strict response ID/QR checking,
    no blocking IO dispatcher. Queue and TCP counts remain identical.
    """
    def __init__(self, host, port=53, workers=28, capacity=32, timeout=8., age=10.):
        self.hosts, self.port = (host,) if isinstance(host, str) else tuple(host), port
        self.workers, self.timeout, self.age = workers, timeout, age
        self.queue = asyncio.Queue(capacity)
        self.tasks = []
        self.transport = None
        self.closed = False
        self.stats = dict(received=0, replies=0, dropped=0, errors=0, expired=0,
                          queue_max=0, active=0, active_max=0, queue_s=0., service_s=0.,
                          query_bytes=0, reply_bytes=0)

    def connection_made(self, transport):
        self.transport = transport
        self.tasks = [asyncio.create_task(self.worker(i)) for i in range(self.workers)]

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

    async def worker(self, index):
        # Sharding is worker-affine, including retry; QUIC sees a pooled path RTT.
        host = self.hosts[index % len(self.hosts)]
        reader = writer = None
        try:
            while True:
                data, peer, entered = await self.queue.get()
                start = time.monotonic()
                self.stats['queue_s'] += start - entered
                self.stats['active'] += 1
                self.stats['active_max'] = max(self.stats['active_max'], self.stats['active'])
                try:
                    for _ in range(2):
                        remaining = min(self.timeout, self.age - (time.monotonic() - entered))
                        if remaining <= 0:
                            self.stats['expired'] += 1
                            break
                        try:
                            async with asyncio.timeout(remaining):
                                if writer is None:
                                    reader, writer = await asyncio.open_connection(
                                        host, self.port, limit=8192)
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
                            await close_writer(writer)
                            reader = writer = None
                finally:
                    self.stats['active'] -= 1
                    self.stats['service_s'] += time.monotonic() - start
                    self.queue.task_done()
        finally:
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


async def socks_open(port, credentials, host, target_port):
    reader, writer = await asyncio.open_connection('127.0.0.1', port)
    try:
        user, password = (s.encode() for s in credentials)
        writer.write(b'\x05\x01\x02')
        await writer.drain()
        if await reader.readexactly(2) != b'\x05\x02':
            raise ValueError('SOCKS method')
        writer.write(b'\x01' + bytes([len(user)]) + user + bytes([len(password)]) + password)
        await writer.drain()
        if await reader.readexactly(2) != b'\x01\x00':
            raise ValueError('SOCKS auth')
        address = host.encode('ascii')
        writer.write(b'\x05\x01\x00\x03' + bytes([len(address)]) + address + struct.pack('!H', target_port))
        await writer.drain()
        header = await reader.readexactly(4)
        if header[:3] != b'\x05\x00\x00':
            raise ValueError('SOCKS connect')
        sizes = {1: 4, 4: 16}
        n = (await reader.readexactly(1))[0] if header[3] == 3 else sizes[header[3]]
        await reader.readexactly(n + 2)
        return reader, writer
    except BaseException:
        await close_writer(writer)
        raise


async def request(args, secret, proxy, kind, size, condition):
    row = dict(kind=kind, condition=condition, requested=size, ok=False, acknowledged=0,
               dns_s=None, connect_s=None, tls_s=None, submitted_s=None, first_byte_s=None,
               submitted=0, max_drain_s=0., drains_over_1s=0, phase='connect')
    # Fixed fresh high-entropy payload, never zero-filled or user content.
    payload = os.urandom(size) if kind == 'up' else b''
    start = time.monotonic()
    writer = None
    try:
        async with asyncio.timeout(args.deadline):
            if proxy:
                reader, writer = await socks_open(*proxy, args.fixture_host, args.fixture_port)
            else:
                reader, writer = await asyncio.open_connection(args.fixture_host, args.fixture_port)
            row['connect_s'] = time.monotonic() - start
            row['phase'] = 'tls'
            context = ssl.create_default_context(cafile=args.fixture_cert)
            await writer.start_tls(context, server_hostname=args.fixture_host,
                                   ssl_handshake_timeout=args.deadline)
            row['tls_s'] = time.monotonic() - start
            row['phase'] = 'body'
            method = 'POST' if kind == 'up' else 'GET'
            path = '/up' if kind == 'up' else '/down?bytes=' + str(size)
            headers = (f'{method} {path} HTTP/1.1\r\nHost: {args.fixture_host}\r\n'
                       f'Authorization: Bearer {secret["fixture_token"]}\r\n'
                       f'Content-Length: {len(payload)}\r\nConnection: close\r\n\r\n')
            writer.transport.set_write_buffer_limits(high=16384, low=4096)
            writer.write(headers.encode('ascii'))
            await writer.drain()
            for offset in range(0, len(payload), 4096):
                before = time.monotonic()
                writer.write(payload[offset:offset + 4096])
                await writer.drain()
                elapsed = time.monotonic() - before
                row['submitted'] += len(payload[offset:offset + 4096])
                row['max_drain_s'] = max(row['max_drain_s'], elapsed)
                row['drains_over_1s'] += elapsed >= 1
            row['submitted_s'] = time.monotonic() - start
            row['phase'] = 'ack' if kind == 'up' else 'download'
            first = await reader.readexactly(1)
            row['first_byte_s'] = time.monotonic() - start
            header = first + await reader.readuntil(b'\r\n\r\n')
            if len(header) > 8192:
                raise ValueError('HTTP header bound')
            lines = header.decode('ascii').split('\r\n')
            row['http'] = int(lines[0].split()[1])
            fields = dict(s.lower().split(': ', 1) for s in lines[1:] if ': ' in s)
            row['fixture_version'] = int(fields['x-fixture-version'])
            row['egress_direct'] = fields['x-egress-direct'] == '1'
            row['egress_fingerprint'] = fields['x-egress-fingerprint']
            if args.egress != 'unchecked' and row['egress_direct'] != (args.egress == 'direct'):
                raise ValueError('Unexpected fixture egress')
            length = int(fields['content-length'])
            if not 0 <= length <= (1024 if kind == 'up' else size):
                raise ValueError('HTTP body bound')
            body = await reader.readexactly(length)
            if row['http'] != 200:
                raise ValueError('HTTP status')
            if kind == 'up':
                ack = json.loads(body)
                if ack != {'bytes': size, 'sha256': hashlib.sha256(payload).hexdigest()}:
                    raise ValueError('Wrong acknowledgement')
            elif length != size or fields.get('x-sha256') != hashlib.sha256(body).hexdigest():
                raise ValueError('Wrong download')
            row.update(ok=True, acknowledged=size, phase='complete')
    except asyncio.CancelledError:
        row['error'] = 'Cancelled'
        raise
    except (OSError, ValueError, KeyError, EOFError, TimeoutError) as error:
        row['error'] = type(error).__name__
    finally:
        row['seconds'] = time.monotonic() - start
        row['ack_Bps'] = row['acknowledged'] / row['seconds']
        await close_writer(writer)
        emit(row)
    return row


async def workload(args, secret, proxy, background_proxy=None):
    rows = []
    if args.workload == 'short-parallel':
        for index in range(2):
            rows.append(await request(args, secret, proxy, 'up', 32768, f'serial-{index}'))
        for index in range(2):
            start = time.monotonic()
            pair = await asyncio.gather(
                request(args, secret, proxy, 'up', 32768, f'parallel-{index}-a'),
                request(args, secret, background_proxy or proxy, 'up', 32768, f'parallel-{index}-b'))
            seconds = time.monotonic() - start
            rows.extend(pair)
            emit(dict(kind='parallel_group', index=index, seconds=seconds,
                      acknowledged=sum(r['acknowledged'] for r in pair), ok=all(r['ok'] for r in pair)))
        rows.append(await request(args, secret, proxy, 'down', 4096, 'recovery'))
        return rows
    for size in SIZES:
        rows.append(await request(args, secret, proxy, 'up', size, 'sequential'))
    rows.append(await request(args, secret, proxy, 'down', 1048576, 'sequential'))
    # Keep the background offered load present for every small upload, not only
    # the first one. One bounded 1MiB flow, replenished until this upload ends.
    for size in SIZES:
        stop = asyncio.Event()
        async def load():
            while not stop.is_set():
                await request(args, secret, background_proxy or proxy, 'down', 1048576, 'background')
        task = asyncio.create_task(load())
        try:
            await asyncio.sleep(.25)
            rows.append(await request(args, secret, proxy, 'up', size, 'loaded'))
        finally:
            stop.set()
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
    rows.append(await request(args, secret, proxy, 'down', 4096, 'recovery'))
    return rows


async def start_client(executable, domain, cert, argv, token, children, drains, cc='dcubic'):
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    credentials = (os.urandom(16).hex(), os.urandom(16).hex())
    child = await asyncio.create_subprocess_exec(str(executable), '--tcp-listen-host', '127.0.0.1',
        '--tcp-listen-port', str(port), '--domain', domain, '--cert', str(cert),
        '--congestion-control', cc, '--flow-relay-stdin', *argv,
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT)
    children.append(child)
    child.stdin.write(token + ''.join(credentials).encode())
    await child.stdin.drain()
    child.stdin.close()
    ready = asyncio.Event()
    async def discard():
        while line := await child.stdout.readline():
            if b'Connection ready' in line:
                ready.set()
            match = re.search(rb'domain_usage suffix=([a-z0-9.-]+) endpoint=(\S+) data=(\d+) polls=(\d+)', line)
            if match:
                emit(dict(kind='domain_usage', suffix=match[1].decode(),
                          endpoint=match[2].decode(), data=int(match[3]), polls=int(match[4])))
    drains.append(asyncio.create_task(discard()))
    await ready.wait()
    return port, credentials


async def run(args, secret):
    paths, children, drains, proxies = [], [], [], []
    proxy = None
    started = time.monotonic()
    manifest = dict(kind='manifest', version=VERSION, mode=args.mode, order=args.order,
                    script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    fixture_cert_sha256=hashlib.sha256(Path(args.fixture_cert).read_bytes()).hexdigest(),
                    python=platform.python_version(), machine=platform.machine(),
                    deadline=args.deadline, sizes=SIZES, workers=56, queue=64, egress=args.egress,
                    topology=args.topology, path_layout=TOPOLOGIES[args.topology],
                     domain_layout=args.domains, connections=args.connections,
                     partition=args.partition, workload=args.workload)
    if args.mode == 'native':
        manifest['native_sha256'] = hashlib.sha256(Path(args.native).read_bytes()).hexdigest()
    emit(manifest)
    try:
        if args.mode == 'native':
            layout = list(TOPOLOGIES[args.topology])
            if args.order == 'reverse':
                layout.reverse()
            suffixes = {'old': ['t.x.ass-peak.de'] * 2,
                        'new': ['tt.x.ass-peak.de'] * 2,
                        'split': ['t.x.ass-peak.de', 'tt.x.ass-peak.de'],
                        'split-reverse': ['tt.x.ass-peak.de', 't.x.ass-peak.de']}
            partitions = args.connections if args.partition else 1
            layouts = []
            for _ in range(partitions):
                argv = []
                for index, (hosts, workers, capacity) in enumerate(layout):
                    path = Adapter(hosts, workers=workers // partitions, capacity=capacity // partitions)
                    transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                        lambda: path, local_addr=('127.0.0.1', 0))
                    paths.append(path)
                    argv += ['--authoritative', '127.0.0.1:' + str(transport.get_extra_info('sockname')[1])]
                    if args.domains != 'legacy':
                        argv += ['--path-domain', argv[-1] + '=' + suffixes[args.domains][index]]
                layouts.append(argv)
            async with asyncio.timeout(15):
                for connection in range(args.connections):
                    proxies.append(await start_client(args.native, args.domain, args.cert, layouts[connection % partitions],
                        bytes.fromhex(secret['flow_token']), children, drains))
            emit(dict(kind='startup', ok=True, seconds=time.monotonic() - started))
            proxy = proxies[0]
        rows = await workload(args, secret, proxy, proxies[-1] if proxies else None)
        emit(dict(kind='window', ok=all(r['ok'] for r in rows), failures=sum(not r['ok'] for r in rows)))
        return all(r['ok'] for r in rows)
    except (OSError, ValueError, EOFError, TimeoutError) as error:
        emit(dict(kind='window', ok=False, error=type(error).__name__, seconds=time.monotonic()-started))
        return False
    finally:
        for child in children:
            if child.returncode is None:
                child.terminate()
                try:
                    await asyncio.wait_for(child.wait(), 2)
                except TimeoutError:
                    child.kill()
                    await child.wait()
        for drain in drains:
            drain.cancel()
        await asyncio.gather(*drains, return_exceptions=True)
        for path in paths:
            await path.close()
        emit(dict(kind='cleanup', child_alive=any(c.returncode is None for c in children),
                  workers_alive=sum(not t.done() for p in paths for t in p.tasks),
                  paths=[p.stats for p in paths]))


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['native', 'gvisor'], required=True)
    parser.add_argument('--native', help='Executable Android arm64 client; no APK rebuild')
    parser.add_argument('--cert', help='Unchanged public carrier certificate')
    parser.add_argument('--domain', default='t.x.ass-peak.de')
    parser.add_argument('--domains', choices=['legacy', 'old', 'new', 'split', 'split-reverse'], default='legacy')
    parser.add_argument('--order', choices=['accepted', 'reverse'], default='accepted')
    parser.add_argument('--topology', choices=TOPOLOGIES, default='accepted')
    parser.add_argument('--connections', type=int, choices=[1, 2], default=1,
                        help='Isolated native experiment only: shared 56/64 adapters, flow-affine clients')
    parser.add_argument('--partition', action='store_true', help='Split same 56/64 adapter budget by connection')
    parser.add_argument('--workload', choices=['full', 'short-parallel'], default='full')
    parser.add_argument('--fixture-host', required=True)
    parser.add_argument('--fixture-port', type=int, default=40004)
    parser.add_argument('--fixture-cert', required=True)
    parser.add_argument('--deadline', type=float, default=40.)
    parser.add_argument('--egress', choices=['direct', 'warp', 'unchecked'], default='direct',
                        help='Fixture source check; WARP also needs independent interface/trace proof')
    args = parser.parse_args()
    ipaddress.ip_address(args.fixture_host)
    if not 1 <= args.fixture_port <= 65535 or not 1 <= args.deadline <= 90:
        parser.error('Port or deadline outside bound')
    if args.mode == 'native' and not (args.native and args.cert):
        parser.error('Native mode requires executable and carrier certificate')
    if args.domains != 'legacy' and (args.mode != 'native' or args.topology != 'accepted'):
        parser.error('Domain experiment requires native mode and accepted two-path budget')
    if args.connections != 1 and (args.mode != 'native' or args.topology != 'accepted' or args.domains != 'legacy'):
        parser.error('Connection experiment requires native accepted topology and legacy domain')
    if args.partition and args.connections != 2:
        parser.error('Partition experiment requires two connections')
    return args


async def main(args, secret):
    task = asyncio.current_task()
    def cancel_once():
        if not task.cancelling():
            task.cancel()
    asyncio.get_running_loop().add_signal_handler(signal.SIGTERM, cancel_once)
    owner = Path(__file__).with_name('owner.pid')
    # Host keeps stdin open. USB/adb loss closes it and cancels the owned runtime.
    pipe = asyncio.StreamReader()
    protocol = asyncio.StreamReaderProtocol(pipe)
    transport, _ = await asyncio.get_running_loop().connect_read_pipe(lambda: protocol, sys.stdin.buffer)
    async def disconnected():
        await pipe.read()
        cancel_once()
    watcher = asyncio.create_task(disconnected())
    owner.write_text(str(os.getpid()))
    try:
        return await run(args, secret)
    finally:
        watcher.cancel()
        await asyncio.gather(watcher, return_exceptions=True)
        transport.close()
        owner.unlink(missing_ok=True)


if __name__ == '__main__':
    args = arguments()
    try:
        secret = json.loads(sys.stdin.buffer.readline(1024))
        if len(bytes.fromhex(secret['fixture_token'])) != 32:
            raise ValueError('fixture credential')
        if args.mode == 'native' and len(bytes.fromhex(secret['flow_token'])) != 16:
            raise ValueError('flow credential')
        sys.exit(0 if asyncio.run(main(args, secret)) else 1)
    except (ValueError, KeyError, OSError):
        emit(dict(kind='input', ok=False))
        sys.exit(2)
    except (KeyboardInterrupt, asyncio.CancelledError):
        sys.exit(130)
