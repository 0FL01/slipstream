"""Local-only protocol, FIN, adapter bound/retry and evidence checks."""
import asyncio
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from adapter import Adapter
from backend import Backend, close_writer
from run import BINARIES, Runner, arguments, carrier_manifest, protected_json, schedule, socks_open, tcp_layout
from summarize import summarize


class Output:
    def __init__(self):
        self.rows = []

    def emit(self, row):
        self.rows.append(row)


class ProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.backend = Backend(timeout=2)
        self.server = await asyncio.start_server(self.backend.handle, '127.0.0.1', 0)
        self.port = self.server.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()
        tasks = list(self.backend.active)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)

    async def test_exact_download_upload_echo_and_independent_fin(self):
        for opcode, size in [('D', 1048576), ('U', 524288), ('E', 8192)]:
            reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
            writer.write(b'AB01' + opcode.encode() + struct.pack('!I', size))
            digest = hashlib.sha256(b'x' * size).digest()
            if opcode in ('U', 'E'):
                writer.write(b'x' * size)
                await writer.drain()
                writer.write_eof()
            if opcode in ('D', 'E'):
                body = await reader.readexactly(size)
                self.assertEqual(body, b'x' * size)
                if opcode == 'D':
                    writer.write(digest)
                    await writer.drain()
                    writer.write_eof()
            if opcode != 'E':
                self.assertEqual(await reader.readexactly(34), b'OK' + digest)
            self.assertEqual(await reader.read(), b'')
            await close_writer(writer)

    async def test_reject_non_synthetic_and_bounds(self):
        for opcode, size, payload in [(b'U', 1, b'y'), (b'U', 1048577, b''), (b'Q', 1, b'')]:
            reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
            writer.write(b'AB01' + opcode + struct.pack('!I', size) + payload)
            await writer.drain()
            self.assertEqual(await reader.read(), b'')
            await close_writer(writer)

    async def test_runner_ack_and_timeout_are_distinct(self):
        args = arguments(['--variant', 'C0', '--server', 'local', '--results', 'unused', '--half-close'])
        out = Output()
        runner = Runner(args, {}, out)
        runner.port = self.port
        for opcode in ('D', 'U', 'E'):
            row = await runner.request(opcode, 8192, 'test')
            self.assertTrue(row['ok'])
            self.assertEqual(row['acknowledged'], 8192)
        stop = asyncio.Event()
        async def stalled(reader, writer):
            await stop.wait()
            await close_writer(writer)
        server = await asyncio.start_server(stalled, '127.0.0.1', 0)
        try:
            runner.port = server.sockets[0].getsockname()[1]
            args.deadline = .05
            row = await runner.request('U', 8192, 'timeout')
            self.assertTrue(row['timeout'])
            self.assertFalse(row['ok'])
            self.assertEqual(row['ack_Bps'], 0)
            self.assertEqual(row['submitted'], 8192)
        finally:
            stop.set()
            server.close()
            await server.wait_closed()

    async def test_complete_local_window(self):
        self.backend.timeout = 45
        args = arguments(['--variant', 'C0', '--server', 'local', '--results', 'unused'])
        out = Output()
        runner = Runner(args, {}, out)
        runner.port, runner.pid = self.port, os.getpid()
        runner.monitor_task = asyncio.create_task(runner.monitor())
        try:
            self.assertTrue(await runner.workload())
        finally:
            await runner.cleanup()
        requests = [r for r in out.rows if r['kind'] == 'request' and r['condition'] != 'background']
        self.assertEqual(len(requests), 12)
        self.assertTrue(all(r['ok'] for r in requests))
        idle = [r for r in out.rows if r['kind'] == 'idle']
        self.assertEqual([r['condition'] for r in idle], ['empty', 'quiet-open', 'settle'])
        self.assertTrue(all(r['queries_per_s'] is None for r in idle))
        window = next(r for r in out.rows if r['kind'] == 'window')
        self.assertEqual(window['failures'], 0)
        self.assertGreater(window['resource_peaks']['rss_bytes'], 0)
        self.assertEqual(out.rows[-1]['workers_alive'], 0)

    async def test_socks_authenticated_connect(self):
        credentials = ('a' * 32, 'b' * 32)
        checked = []
        async def handle(reader, writer):
            try:
                self.assertEqual(await reader.readexactly(3), b'\x05\x01\x02')
                writer.write(b'\x05\x02')
                await writer.drain()
                self.assertEqual(await reader.readexactly(67), b'\x01\x20' + b'a' * 32 + b'\x20' + b'b' * 32)
                writer.write(b'\x01\x00')
                await writer.drain()
                self.assertEqual(await reader.readexactly(10), b'\x05\x01\x00\x01\x7f\x00\x00\x01' + struct.pack('!H', 40100))
                checked.append(True)
                writer.write(b'\x05\x00\x00\x01' + b'\0' * 6)
                await writer.drain()
            finally:
                await close_writer(writer)
        server = await asyncio.start_server(handle, '127.0.0.1', 0)
        try:
            _, writer = await socks_open(server.sockets[0].getsockname()[1], credentials, '127.0.0.1', 40100)
            await close_writer(writer)
            self.assertEqual(checked, [True])
        finally:
            server.close()
            await server.wait_closed()

    async def test_dns_id_qr_cap_retry_persistence(self):
        for fault in ('id', 'qr', 'cap'):
            connections = 0
            queries = 0
            async def dns(reader, writer):
                nonlocal connections, queries
                connections += 1
                try:
                    while True:
                        size = struct.unpack('!H', await reader.readexactly(2))[0]
                        query = await reader.readexactly(size)
                        queries += 1
                        reply = query[:2] + b'\x80' + query[3:]
                        if queries == 1:
                            if fault == 'id':
                                reply = b'\xff\xff' + reply[2:]
                            elif fault == 'qr':
                                reply = query
                            else:
                                writer.write(struct.pack('!H', 4097))
                                await writer.drain()
                                continue
                        writer.write(struct.pack('!H', len(reply)) + reply)
                        await writer.drain()
                except (OSError, EOFError):
                    pass
                finally:
                    await close_writer(writer)
            server = await asyncio.start_server(dns, '127.0.0.1', 0)
            adapter = Adapter('127.0.0.1', server.sockets[0].getsockname()[1], workers=1, capacity=1)
            transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                lambda: adapter, local_addr=('127.0.0.1', 0))
            sock = __import__('socket').socket(__import__('socket').AF_INET, __import__('socket').SOCK_DGRAM)
            sock.bind(('127.0.0.1', 0))
            sock.setblocking(False)
            try:
                for index in range(2):
                    query = struct.pack('!H', index) + b'\0' * 10
                    sock.sendto(query, transport.get_extra_info('sockname'))
                    reply = await asyncio.wait_for(asyncio.get_running_loop().sock_recv(sock, 4096), 1)
                    self.assertEqual(reply[:2], query[:2])
                    self.assertTrue(reply[2] & 0x80)
                self.assertEqual(adapter.stats['retries'], 1)
                self.assertEqual(adapter.stats['errors'], 1)
                self.assertEqual(connections, 2)
                self.assertEqual(queries, 3)
                # Synchronous submissions before workers run exercise drop-new.
                for _ in range(3):
                    adapter.datagram_received(b'\0' * 12, sock.getsockname())
                self.assertEqual(adapter.stats['dropped'], 2)
                self.assertEqual(adapter.queue.qsize(), 1)
            finally:
                sock.close()
                await adapter.close()
                server.close()
                await server.wait_closed()
            self.assertTrue(all(task.done() for task in adapter.tasks))

    async def test_runner_allocates_distinct_ordered_tcp_adapters(self):
        for count in (1, 2, 3, 5):
            with self.subTest(paths=count):
                endpoints = [('127.0.0.1', 5303), ('127.0.0.2', 1), ('127.0.0.1', 65535),
                             ('127.0.0.3', 5305), ('127.0.0.1', 5306)][:count]
                argv = ['--variant', 'C1', '--server', 'local', '--results', 'unused',
                        '--credentials', 'unused', '--cert', 'unused', '--execution', 'native',
                        '--carrier', 'multipath', '--tcp-workers', '112']
                for host, port in endpoints:
                    argv += ['--tcp-resolver', f'{host}:{port}']
                args = arguments(argv)
                out = Output()
                runner = Runner(args, {}, out)
                try:
                    # Stop at native spawn: no DNS queries or external processes.
                    with patch('run.asyncio.create_subprocess_exec',
                               side_effect=ValueError('test spawn stopped')) as spawn:
                        with self.assertRaisesRegex(ValueError, 'test spawn stopped'):
                            await runner.start()
                    self.assertEqual([(path.host, path.port, path.workers, path.queue.maxsize)
                                      for path in runner.paths], tcp_layout(args))
                    local = [path.transport.get_extra_info('sockname') for path in runner.paths]
                    self.assertEqual(len(set(local)), count)
                    self.assertTrue(all(host == '127.0.0.1' for host, _ in local))
                    command = spawn.call_args.args
                    self.assertEqual([command[index + 1] for index, value in enumerate(command)
                                      if value == '--resolver'],
                                     [f'{host}:{port}' for host, port in local])
                finally:
                    await runner.cleanup()
                self.assertEqual(out.rows[-1]['workers_alive'], 0)


class EvidenceTests(unittest.TestCase):
    def test_parser_and_frozen_orders(self):
        self.assertEqual(len(schedule('aa')), 2)
        self.assertEqual(''.join(item[2] for item in schedule('screen')), 'ABBA')
        self.assertEqual(''.join(item[2] for item in schedule('confirm')), 'ABBAABBAABBA')
        self.assertEqual(BINARIES['C0'].parent.name, 'build')
        self.assertEqual(BINARIES['C0'].parent.parent.name, 'slipstream-c-baseline')
        self.assertEqual(arguments(['--schedule', '--phase', 'confirm']).phase, 'confirm')

    def test_tcp_presets_and_resolver_budget_overrides(self):
        base = ['--variant', 'C0', '--server', 'local', '--results', 'unused']
        for carrier, expected in (
                ('direct', []),
                ('recursive', [('77.88.8.88', 53, 56, 64)]),
                ('multipath', [('77.88.8.88', 53, 28, 32), ('77.88.8.1', 53, 28, 32)])):
            with self.subTest(preset=carrier):
                args = arguments(base + ['--carrier', carrier])
                self.assertEqual(tcp_layout(args), expected)
                manifest = carrier_manifest(args)
                self.assertEqual(manifest['paths'], len(expected) or 1)
                self.assertEqual(manifest['workers'], 56 if expected else 0)
                self.assertEqual(manifest['queue'], 64 if expected else 0)
                self.assertEqual(len(manifest['tcp_paths']), len(expected))
        for total, count in ((56, 1), (112, 2), (224, 3), (224, 5)):
            with self.subTest(workers=total, paths=count):
                endpoints = [('127.0.0.1', 5303), ('127.0.0.2', 53), ('127.0.0.1', 5304),
                             ('127.0.0.3', 5305), ('127.0.0.1', 5306)][:count]
                argv = base + ['--carrier', 'multipath', '--tcp-workers', str(total)]
                for host, port in endpoints:
                    argv += ['--tcp-resolver', f'{host}:{port}']
                args = arguments(argv)
                layout = tcp_layout(args)
                workers, worker_extra = divmod(total, count)
                capacity, capacity_extra = divmod(64, count)
                self.assertEqual(layout, [(host, port,
                    workers + (index < worker_extra), capacity + (index < capacity_extra))
                    for index, (host, port) in enumerate(endpoints)])
                manifest = carrier_manifest(args)
                self.assertEqual((manifest['workers'], manifest['queue'], manifest['paths']),
                                 (total, 64, count))
                self.assertEqual(manifest['tcp_paths'], [dict(
                    endpoint_sha256=hashlib.sha256(f'{host}:{port}'.encode()).hexdigest(),
                    workers=layout[index][2], queue=layout[index][3])
                    for index, (host, port) in enumerate(endpoints)])
                self.assertNotIn('127.0.0.1', json.dumps(manifest))

    def test_tcp_resolver_rejects_invalid_endpoints_and_budgets(self):
        base = ['--variant', 'C0', '--server', 'local', '--results', 'unused', '--carrier', 'multipath']
        invalid = [['--tcp-resolver', endpoint] for endpoint in (
            '127.0.0.1', 'localhost:53', '[::1]:53', '127.0.0.1:0', '127.0.0.1:65536')]
        invalid += [['--tcp-workers', '57']]
        invalid += [
            ['--tcp-resolver'],
            ['--tcp-resolver', '127.0.0.1:53', '--tcp-resolver', '127.0.0.1:053'],
            ['--tcp-resolver', '127.0.0.1:53', '--carrier', 'direct'],
            [value for index in range(6) for value in ('--tcp-resolver', f'127.0.0.1:{5301 + index}')]]
        for extra in invalid:
            with self.subTest(options=extra), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    arguments(base + extra)
                self.assertEqual(error.exception.code, 2)

    def test_protected_credentials(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as directory:
            path = Path(directory) / 'credentials'
            path.write_text(json.dumps({'flow_token': '00' * 16}))
            path.chmod(0o600)
            self.assertIn('flow_token', protected_json(str(path)))
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                protected_json(str(path))

    def test_failed_candidate_zero_and_failed_control_undefined(self):
        rows = []
        for pair in range(6):
            for arm in ('A', 'B'):
                ok = not (pair == 0 and arm == 'B' or pair == 1 and arm == 'A')
                rows.append(dict(kind='request', campaign='test', phase='confirm', carrier='direct',
                    backend='transport', pair=pair, arm=arm, variant='C0' if arm == 'A' else 'C1',
                    server='control' if arm == 'A' else 'candidate', sample=0, condition='sequential',
                    opcode='D', requested=1048576, ack_Bps=(100 if arm == 'A' else 120) if ok else 0,
                    ok=ok, timeout=not ok, seconds=1))
        result = summarize(rows, 'sequential:D:1048576')
        self.assertEqual(result['pairs'][0]['ratio_B_over_A'], 0)
        self.assertIsNone(result['pairs'][1]['ratio_B_over_A'])
        self.assertFalse(result['confirmation'][0]['throughput_gate'])
        self.assertEqual(sum(row['timeouts'] for row in result['variants']), 2)


if __name__ == '__main__':
    unittest.main()
