"""Live frozen C2 acceptance: fixed authenticated fixture only, aggregate output."""
import asyncio
import hashlib
import ipaddress
import json
import os
import secrets
import socket
import struct
import subprocess
import time
import sys
from pathlib import Path
from types import SimpleNamespace

from adapter import Adapter
from backend import close_writer
from run import BINARIES, Runner, protected_json

BASE = Path(__file__).resolve().parent
HOST = '195.128.101.186'
PORT = 40004
SECRET = protected_json(str(BASE / '.warp.credentials.json'))


class Sink:
    def emit(self, row):
        pass


class Client:
    def __init__(self, suffix='t.x.ass-peak.de', wrong_token=False, wrong_pin=False,
                 dead_primary=False, both_dead=False, direct=False):
        self.suffix, self.wrong_pin = suffix, wrong_pin
        token = bytes.fromhex(SECRET['flow_token'])
        if wrong_token:
            token = bytes([token[0] ^ 1]) + token[1:]
        self.user, self.password = secrets.token_hex(16).encode(), secrets.token_hex(16).encode()
        self.stdin = token + self.user + self.password
        self.dead_primary, self.both_dead = dead_primary, both_dead
        self.direct = direct
        self.adapters, self.transports, self.child = [], [], None

    async def __aenter__(self):
        endpoints = []
        for index in range(0 if self.direct else 2 if self.dead_primary or self.both_dead else 1):
            dead = self.both_dead or (self.dead_primary and index == 0)
            path = asyncio.DatagramProtocol() if dead else Adapter('77.88.8.88' if index == 0 else '77.88.8.1', workers=28 if self.dead_primary else 56, capacity=32 if self.dead_primary else 64)
            transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(lambda: path, local_addr=('127.0.0.1', 0))
            self.transports.append(transport)
            if not dead:
                self.adapters.append(path)
            endpoints.append('127.0.0.1:' + str(transport.get_extra_info('sockname')[1]))
        if self.direct:
            endpoints.append(HOST + ':53')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            self.port = sock.getsockname()[1]
        argv = [str(BINARIES['C1']), '--tcp-listen-host', '127.0.0.1', '--tcp-listen-port', str(self.port),
                '--domain', self.suffix, '--cert', str(BASE / ('fixture.pem' if self.wrong_pin else 'carrier.pem')),
                '--flow-relay-stdin', '--congestion-control', 'dcubic']
        for endpoint in endpoints:
            argv.extend(['--resolver', endpoint])
        self.child = await asyncio.create_subprocess_exec(*argv, stdin=asyncio.subprocess.PIPE,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        env={key:value for key,value in os.environ.items() if key not in
                             ('SSLKEYLOGFILE', 'QLOGDIR', 'QLOG_DIR', 'RUST_LOG')})
        self.child.stdin.write(self.stdin)
        await self.child.stdin.drain()
        self.child.stdin.close()
        await asyncio.sleep(.15)
        return self

    async def __aexit__(self, *args):
        if self.child and self.child.returncode is None:
            self.child.terminate()
            try:
                await asyncio.wait_for(self.child.wait(), 3)
            except TimeoutError:
                self.child.kill()
                await self.child.wait()
        for adapter in self.adapters:
            await adapter.close()
        for transport in self.transports:
            transport.close()

    async def socks(self, uot=False, wrong_password=False):
        async with asyncio.timeout(10):
            while True:
                try:
                    reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
                    break
                except OSError:
                    await asyncio.sleep(.05)
            try:
                writer.write(b'\x05\x01\x02')
                await writer.drain()
                assert await reader.readexactly(2) == b'\x05\x02'
                password = b'0'*32 if wrong_password else self.password
                writer.write(b'\x01\x20' + self.user + b'\x20' + password)
                await writer.drain()
                auth = await reader.readexactly(2)
                if wrong_password:
                    assert auth == b'\x01\x01'
                    assert await reader.read(1) == b''
                    return reader, writer
                assert auth == b'\x01\x00'
                if uot:
                    name = b'sp.v2.udp-over-tcp.arpa'
                    command = b'\x05\x01\x00\x03' + bytes([len(name)]) + name + b'\x00\x00'
                else:
                    command = b'\x05\x01\x00\x01' + ipaddress.ip_address(HOST).packed + struct.pack('!H', PORT)
                writer.write(command)
                await writer.drain()
                assert await reader.readexactly(10) == b'\x05\x00\x00\x01' + b'\x00'*6
                if uot:
                    writer.write(b'\x00\x01' + ipaddress.ip_address(HOST).packed + struct.pack('!H', PORT))
                    await writer.drain()
                return reader, writer
            except BaseException:
                await close_writer(writer)
                raise

    async def tcp(self):
        reader, writer = await self.socks()
        args = SimpleNamespace(fixture_cert=str(BASE/'fixture.pem'), target_host=HOST, deadline=40, egress='warp')
        runner = Runner(args, SECRET, Sink())
        row = dict(submitted=0, _start=time.monotonic())
        try:
            async with asyncio.timeout(40):
                await runner.https(reader, writer, 'U', 8192, row)
            return dict(acknowledged=8192, digest_exact=True, pinned_inner_tls=True, non_direct_source_flag=True)
        finally:
            await close_writer(writer)

    async def udp(self, unauthorized=False):
        reader, writer = await self.socks(uot=True)
        token = bytes.fromhex(SECRET['fixture_token'])
        if unauthorized:
            token = bytes([token[0]^1]) + token[1:]
        payload = token + b'fixed-authenticated-synthetic-echo-' + secrets.token_bytes(32)
        record = b'\x00' + ipaddress.ip_address(HOST).packed + struct.pack('!HH', PORT, len(payload)) + payload
        try:
            writer.write(record)
            await writer.drain()
            async with asyncio.timeout(5 if unauthorized else 40):
                address_type = (await reader.readexactly(1))[0]
                assert address_type == 0
                address = await reader.readexactly(4)
                port, size = struct.unpack('!HH', await reader.readexactly(4))
                reply = await reader.readexactly(size)
                assert address == ipaddress.ip_address(HOST).packed and port == PORT
                assert reply == payload
                if unauthorized:
                    raise AssertionError('unauthorized fixture echoed')
                return dict(acknowledged=len(payload), exact=True, native_uot=True, fixed_destination=True)
        except TimeoutError:
            if unauthorized:
                return dict(rejected=True, no_reply=True)
            raise
        finally:
            await close_writer(writer)


async def main():
    if '--secondary-only' in sys.argv:
        start = time.monotonic()
        try:
            async with Client(suffix='tt.x.ass-peak.de', direct=True) as client:
                result = await client.tcp()
            row = dict(check='secondary-suffix-direct-tcp-warp', ok=True, seconds=time.monotonic()-start, **result)
        except Exception as error:
            row = dict(check='secondary-suffix-direct-tcp-warp', ok=False, error=type(error).__name__, seconds=time.monotonic()-start)
        evidence = json.loads((BASE/'acceptance.json').read_text())
        evidence['checks'].append(row)
        (BASE/'acceptance.json').write_text(json.dumps(evidence, indent=2)+'\n')
        print(json.dumps(row))
        return row['ok']
    results = []
    async def check(name, fn):
        start = time.monotonic()
        try:
            result = await fn()
            row = dict(check=name, ok=True, seconds=time.monotonic()-start, **(result or {}))
        except Exception as error:
            row = dict(check=name, ok=False, error=type(error).__name__, seconds=time.monotonic()-start)
        results.append(row)
        print(json.dumps(row))

    async with Client() as client:
        await check('tcp-warp-pinned-fixture', client.tcp)
        await check('native-uot-warp-authenticated-echo', client.udp)
        await check('udp-wrong-fixture-token', lambda: client.udp(unauthorized=True))
        async def bad_password():
            _, writer = await client.socks(wrong_password=True)
            await close_writer(writer)
            return dict(rejected=True)
        await check('wrong-socks-password', bad_password)

    async def wrong_flow():
        async with Client(wrong_token=True) as client:
            reader, writer = await client.socks()
            try:
                writer.write(b'synthetic-unauthorized')
                await writer.drain()
                async with asyncio.timeout(15):
                    assert await reader.read(1) == b''
                return dict(rejected=True, no_payload=True)
            finally:
                await close_writer(writer)
    await check('wrong-flow-token', wrong_flow)

    async def stopped(**kwargs):
        async with Client(**kwargs) as client:
            status = await asyncio.wait_for(client.child.wait(), 12)
            assert status > 0
            return dict(rejected=True, exit_status=status)
    await check('wrong-carrier-pin', lambda: stopped(wrong_pin=True))
    await check('multipath-both-dead', lambda: stopped(both_dead=True))
    async def bootstrap():
        async with Client(dead_primary=True) as client:
            result = await client.tcp()
            assert client.child.returncode is None
            return dict(**result, dead_primary=True, live_secondary=True)
    await check('multipath-dead-primary-live-secondary', bootstrap)
    async def secondary_suffix():
        async with Client(suffix='tt.x.ass-peak.de') as client:
            return await client.tcp()
    await check('secondary-suffix-tcp-warp', secondary_suffix)
    async def direct_secondary_suffix():
        async with Client(suffix='tt.x.ass-peak.de', direct=True) as client:
            return await client.tcp()
    await check('secondary-suffix-direct-tcp-warp', direct_secondary_suffix)
    (BASE/'acceptance.json').write_text(json.dumps(dict(client_sha256=hashlib.sha256(BINARIES['C1'].read_bytes()).hexdigest(),
        server='C2-credit-lowwater', source_interface_proof='Parent verified pinned TLS through CloudflareWARP-bound socket; no peer metadata retained', checks=results), indent=2)+'\n')
    return all(row['ok'] for row in results)


if __name__ == '__main__':
    raise SystemExit(0 if asyncio.run(main()) else 1)
