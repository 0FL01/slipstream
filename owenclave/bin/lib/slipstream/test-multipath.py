"""Loopback-only real native multipath faults; discard child output and payloads."""
import asyncio
import json
import os
import signal
import socket
import struct
import sys
from pathlib import Path

BIN = Path(os.environ['SLIPSTREAM_TEST_BIN'])
CERT = Path(os.environ['SLIPSTREAM_TEST_CERTS'])
DOMAINS = ('t.x.ass-peak.de', 'tt.x.ass-peak.de') if os.environ.get('MULTIDOMAIN') == '1' else ('test.com', 'test.com')


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class PathProxy(asyncio.DatagramProtocol):
    def __init__(self, upstream, delay=0, dead=False, domain=None):
        self.upstream = upstream
        self.delay = delay
        self.dead = dead
        self.peer = None
        self.tx = self.rx = self.dropped = 0
        self.pending = set()
        self.domain = domain
        self.domain_mismatch = 0

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        response = addr == self.upstream
        if response:
            self.rx += 1
            target = self.peer
        else:
            self.tx += 1
            if self.domain:
                labels, offset = [], 12
                while offset < len(data) and data[offset]:
                    n = data[offset]
                    labels.append(data[offset+1:offset+1+n])
                    offset += n + 1
                # Compare only; never retain or emit the payload-bearing name.
                suffix = b'.'.join(labels[-len(self.domain.split('.')):]).lower()
                self.domain_mismatch += suffix != self.domain.encode()
            self.peer = addr
            target = self.upstream
        if self.dead or target is None or len(self.pending) >= 128:
            self.dropped += 1
            return
        loop = asyncio.get_running_loop()
        # Alternating delay reorders packets; every seventh reply is duplicated.
        delay = self.delay * (1 if (self.rx + self.tx) % 2 else 2)
        def send():
            self.pending.discard(handle)
            if not self.dead:
                self.transport.sendto(data, target)
                if response and self.rx % 7 == 0:
                    self.transport.sendto(data, target)
        handle = loop.call_later(delay, send)
        self.pending.add(handle)

    def close(self):
        for handle in self.pending:
            handle.cancel()
        self.pending.clear()
        self.transport.close()


async def check(mode):
    token, user, password = os.urandom(16), os.urandom(16).hex().encode(), os.urandom(16).hex().encode()
    received = []
    connections = set()
    async def echo(reader, writer):
        connections.add(writer)
        try:
            header = await reader.readexactly(23)
            assert header == b'\x20' + token + b'\x7f\x00\x00\x01\x01\xbb'
            count = 0
            while data := await reader.read(16384):
                count += len(data)
                writer.write(data)
                await writer.drain()
            writer.write_eof()
            received.append(count)
        finally:
            writer.close()
            await writer.wait_closed()
            connections.discard(writer)
    target = await asyncio.start_server(echo, '127.0.0.1', 0)
    dnsport, socksport = port(), port()
    procs, paths = [], []
    stage = 'spawn'
    try:
        debug = os.environ.get('DEBUG_NATIVE') == '1'
        prefix = ['gdb', '--batch', '-ex', 'set print frame-arguments none', '-ex', 'run', '-ex', 'bt 12', '--args'] if debug else []
        server = await asyncio.create_subprocess_exec(*prefix, os.environ.get('SERVER_BINARY', str(BIN / 'slipstream-server')),
            '--dns-listen-host', '127.0.0.1', '--dns-listen-port', str(dnsport),
            '--target-address', f'127.0.0.1:{target.sockets[0].getsockname()[1]}',
            '--domain', DOMAINS[0], '--domain', DOMAINS[1], '--cert', str(CERT / 'cert.pem'), '--key', str(CERT / 'key.pem'),
            stdout=asyncio.subprocess.PIPE if debug else asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL, start_new_session=True)
        procs.append(server)
        async def drain_debug():
            frames = []
            while line := await server.stdout.readline():
                if line.startswith(b'#') and len(frames) < 12:
                    frames.append(line.decode(errors='replace').strip())
            if frames:
                print(json.dumps({'local_server_stack': frames}), flush=True)
        if debug:
            asyncio.create_task(drain_debug())
        args = []
        for i in range(2):
            proxy = PathProxy(('127.0.0.1', dnsport), .005 if i else 0,
                              mode == 'both-dead' or (mode == 'dead-primary' and i == 0), DOMAINS[i])
            transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                lambda: proxy, local_addr=('127.0.0.1', 0))
            paths.append(proxy)
            args += ['--authoritative', f'127.0.0.1:{transport.get_extra_info("sockname")[1]}']
            if os.environ.get('MULTIDOMAIN') == '1':
                args += ['--path-domain', args[-1] + '=' + DOMAINS[i]]
        client = await asyncio.create_subprocess_exec(str(BIN / 'slipstream-client'),
            '--tcp-listen-host', '127.0.0.1', '--tcp-listen-port', str(socksport),
            '--domain', DOMAINS[0], '--cert', str(CERT / ('alt_cert.pem' if mode == 'wrong-pin' else 'cert.pem')),
            '--congestion-control', 'dcubic', '--flow-relay-stdin', *args,
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT, start_new_session=True)
        procs.append(client)
        client.stdin.write(token + user + password)
        await client.stdin.drain()
        client.stdin.close()
        ready = asyncio.Event()
        async def drain():
            while line := await client.stdout.readline():
                if b'Connection ready' in line:
                    ready.set()
        drain_task = asyncio.create_task(drain())
        stage = 'readiness'
        if mode in {'both-dead', 'wrong-pin'}:
            await asyncio.wait_for(client.wait(), 10)
            await drain_task
            assert not ready.is_set() and not received and not connections
            assert client.returncode != 0 and server.returncode is None
            return {'mode': mode, 'fail_closed': True, 'client_exit': client.returncode,
                    'payload_bytes': 0}
        await asyncio.wait_for(ready.wait(), 10)
        stage = 'socks'
        reader, writer = await asyncio.open_connection('127.0.0.1', socksport)
        writer.write(b'\x05\x01\x02')
        await writer.drain()
        assert await reader.readexactly(2) == b'\x05\x02'
        writer.write(b'\x01\x20' + user + b'\x20' + password)
        await writer.drain()
        assert await reader.readexactly(2) == b'\x01\x00'
        writer.write(b'\x05\x01\x00\x01\x7f\x00\x00\x01\x01\xbb')
        await writer.drain()
        assert (await reader.readexactly(10))[:2] == b'\x05\x00'
        data = os.urandom(65536)
        stage = 'first-payload'
        writer.write(data)
        await writer.drain()
        assert await reader.readexactly(len(data)) == data
        await asyncio.sleep(1)
        if mode == 'fail-after-ready':
            paths[0].dead = True
        stage = 'second-payload'
        writer.write(data)
        await writer.drain()
        writer.write_eof()
        assert await reader.readexactly(len(data)) == data
        stage = 'half-close'
        assert await reader.read() == b''
        writer.close()
        await writer.wait_closed()
        assert received == [2 * len(data)]
        assert not any(p.domain_mismatch for p in paths)
        assert all(p.tx > 0 for p in paths)
        return {'mode': mode, 'exact_bidirectional_bytes': 2 * len(data), 'half_close': True,
                'paths': [{'queries': p.tx, 'responses': p.rx, 'drops': p.dropped} for p in paths]}
    except BaseException:
        print(json.dumps({'failed_stage': stage, 'mode': mode,
                          'exits': [p.returncode for p in procs],
                          'paths': [{'queries': p.tx, 'responses': p.rx, 'drops': p.dropped} for p in paths]}), flush=True)
        raise
    finally:
        for p in procs:
            if p.returncode is None:
                os.killpg(p.pid, signal.SIGTERM)
        for p in procs:
            try:
                await asyncio.wait_for(p.wait(), 2)
            except asyncio.TimeoutError:
                os.killpg(p.pid, signal.SIGKILL)
                await p.wait()
        for p in paths:
            p.close()
        for writer in list(connections):
            writer.close()
        target.close()
        await target.wait_closed()


async def main():
    failed = False
    for mode in (('fail-after-ready',) if os.environ.get('DEBUG_NATIVE') == '1' else ('reorder-duplicate', 'fail-after-ready', 'dead-primary', 'both-dead', 'wrong-pin')):
        try:
            result = await asyncio.wait_for(check(mode), 20)
        except Exception as error:
            result = {'mode': mode, 'failed': type(error).__name__}
            failed = True
        print(json.dumps(result), flush=True)
    return int(failed)


if __name__ == '__main__':
    sys.exit(asyncio.run(main()))
