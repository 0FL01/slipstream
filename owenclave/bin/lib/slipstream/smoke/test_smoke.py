import asyncio
import contextlib
import io
import json
from pathlib import Path
import ssl
import struct
import subprocess
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import device
import causal
import host
import report
from fixture import Fixture


class HostTests(unittest.TestCase):
    def test_causal_rejects_mislabelled_experiment_before_starting_children(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'must-not-exist.jsonl'
            base = [sys.executable, str(Path(causal.__file__)), '--client', sys.executable,
                    '--server', sys.executable, '--output', str(output)]
            for extra in (['--partition'], ['--focused', '--workload', 'short-parallel']):
                result = subprocess.run(base + extra, capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(output.exists())

    def test_connection_experiment_is_explicit_native_only(self):
        base = ['device', '--mode', 'native', '--native', 'client', '--cert', 'carrier.crt',
                '--fixture-host', '127.0.0.1', '--fixture-cert', 'fixture.crt']
        with patch.object(sys, 'argv', base):
            self.assertEqual(device.arguments().connections, 1)
        with patch.object(sys, 'argv', base + ['--connections', '2']):
            self.assertEqual(device.arguments().connections, 2)
        with patch.object(sys, 'argv', base + ['--partition']), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            device.arguments()
        for extra in (['--mode', 'gvisor'], ['--topology', 'third'], ['--domains', 'split'], ['--connections', '3']):
            with patch.object(sys, 'argv', base + ['--connections', '2'] + extra), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                device.arguments()

    def test_domain_experiment_requires_native_two_path_budget(self):
        base = ['device', '--mode', 'native', '--native', 'client', '--cert', 'carrier.crt',
                '--fixture-host', '127.0.0.1', '--fixture-cert', 'fixture.crt']
        for domain in ('legacy', 'old', 'new', 'split', 'split-reverse'):
            with patch.object(sys, 'argv', base + ['--domains', domain]):
                self.assertEqual(device.arguments().domains, domain)
        for extra in (['--mode', 'gvisor'], ['--topology', 'third'], ['--topology', 'shard']):
            with patch.object(sys, 'argv', base + ['--domains', 'split'] + extra), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                device.arguments()

    def test_domain_report_allows_cross_arm_but_rejects_within_arm_drift(self):
        def window(path):
            domain = 'split' if 'candidate' in path.name else 'old'
            return dict(domain_layout=domain, deadline=40), {}, []
        argv = ['report', '--directory', '.', '--label', 'domain', '--pairs', '2']
        with patch.object(sys, 'argv', argv), patch.object(report, 'read_window', window), \
                contextlib.redirect_stdout(io.StringIO()):
            report.main()
        def drift(path):
            manifest, requests, rows = window(path)
            if 'p2-candidate' in path.name:
                manifest['domain_layout'] = 'split-reverse'
            return manifest, requests, rows
        with patch.object(sys, 'argv', argv), patch.object(report, 'read_window', drift), \
                self.assertRaisesRegex(ValueError, 'Manifest drift within variant'):
            report.main()

    def test_topologies_keep_aggregate_bounds(self):
        for layout in device.TOPOLOGIES.values():
            self.assertEqual(sum(workers for _, workers, _ in layout), 56)
            self.assertEqual(sum(capacity for _, _, capacity in layout), 64)
            self.assertLessEqual(len(layout), 3)
            for hosts, workers, capacity in layout:
                self.assertGreater(capacity, 0)
                self.assertEqual(workers % len(hosts), 0)

    def test_report_keeps_background_failures_separate_from_intended_cancellation(self):
        def window(path):
            return dict(workload='short-parallel'), {}, [
                dict(kind='down', condition='background', ok=False, error='TimeoutError', phase='tls', seconds=40),
                dict(kind='down', condition='background', ok=False, error='Cancelled', phase='download', seconds=1)]
        output = io.StringIO()
        with patch.object(sys, 'argv', ['report', '--directory', '.', '--label', 'test', '--pairs', '1']), \
                patch.object(report, 'read_window', window), contextlib.redirect_stdout(output):
            report.main()
        for variant in json.loads(output.getvalue())['variants'].values():
            self.assertEqual(variant['background_failures'], [dict(error='TimeoutError', phase='tls', seconds=40)])

    def test_active_network_agent_not_callback_or_not_vpn_capability(self):
        self.assertFalse(host.active_vpn('NetworkAgentInfo{ Transports: CELLULAR Capabilities: NOT_VPN }'))
        self.assertFalse(host.active_vpn('NetworkRequest Transports: VPN Capabilities: INTERNET'))
        self.assertTrue(host.active_vpn('NetworkAgentInfo{ Transports: CELLULAR|VPN Capabilities: INTERNET }'))
        self.assertTrue(host.active_vpn('NetworkAgentInfo{ Transports: VPN Capabilities: INTERNET }'))

    def test_failures_not_positive_ratios_and_incomplete_window_rejected(self):
        self.assertEqual(report.paired([-100, 5, None])['n'], 2)
        self.assertEqual(report.paired([-100, 5, None])['wins'], 1)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'failed.jsonl'
            path.touch()
            with self.assertRaises(ValueError):
                report.read_window(path)


class AdapterTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.connections = set()
        self.calls = 0
        self.behavior = 'good'
        async def dns(reader, writer):
            self.connections.add(writer)
            try:
                while True:
                    n = struct.unpack('!H', await reader.readexactly(2))[0]
                    data = await reader.readexactly(n)
                    self.calls += 1
                    if self.behavior == 'stall':
                        await reader.read()
                        return
                    reply = data[:2] + b'\x80' + data[3:]
                    if self.behavior == 'id':
                        reply = b'zz' + reply[2:]
                    if self.behavior == 'length':
                        writer.write(b'\xff\xff')
                    elif self.behavior == 'truncated':
                        writer.write(struct.pack('!H', len(reply)) + reply[:3])
                        return
                    else:
                        # Fragmented TCP frames must preserve datagram affinity.
                        writer.write(struct.pack('!H', len(reply))[:1])
                        await writer.drain()
                        await asyncio.sleep(.001)
                        writer.write(struct.pack('!H', len(reply))[1:] + reply)
                    await writer.drain()
            except (OSError, EOFError):
                pass
            finally:
                await device.close_writer(writer)
                self.connections.discard(writer)
        self.server = await asyncio.start_server(dns, '127.0.0.1', 0)
        self.adapter = device.Adapter('127.0.0.1', self.server.sockets[0].getsockname()[1],
                                      workers=2, capacity=2, timeout=.05, age=.11)
        self.transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            lambda: self.adapter, local_addr=('127.0.0.1', 0))
        self.received = asyncio.Queue()
        owner = self
        class Receiver(asyncio.DatagramProtocol):
            def datagram_received(self, data, peer):
                owner.received.put_nowait((data, peer))
        self.clients = []
        for _ in range(2):
            client, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                Receiver, local_addr=('127.0.0.1', 0))
            self.clients.append(client)

    async def asyncTearDown(self):
        await self.adapter.close()
        for client in self.clients:
            client.close()
        self.server.close()
        await self.server.wait_closed()
        for writer in list(self.connections):
            await device.close_writer(writer)
        await asyncio.sleep(.02)

    def send(self, ident=1, client=0):
        self.clients[client].sendto(struct.pack('!H', ident) + bytes(10),
                                   self.transport.get_extra_info('sockname'))

    async def test_persistence_fragmentation_and_loopback_reply(self):
        for i in range(6):
            self.send(i, i % 2)
            reply, source = await asyncio.wait_for(self.received.get(), 1)
            self.assertEqual(reply[:2], struct.pack('!H', i))
            self.assertEqual(source, self.transport.get_extra_info('sockname'))
        self.assertLessEqual(len(self.connections), 2)
        self.assertEqual(self.calls, 6)

    async def test_malformed_responses_retry_twice_without_reply(self):
        for behavior in ('id', 'length', 'truncated'):
            self.behavior = behavior
            before = self.calls
            self.send()
            await asyncio.sleep(.005)
            await asyncio.wait_for(self.adapter.queue.join(), 1)
            self.assertEqual(self.calls - before, 2)
            self.assertTrue(self.received.empty())

    async def test_path_stall_queue_bounds_cancel_cleanup(self):
        self.behavior = 'stall'
        for _ in range(30):
            self.send()
        await asyncio.sleep(.02)
        self.assertLessEqual(self.adapter.stats['queue_max'], 2)
        self.assertLessEqual(self.adapter.stats['active_max'], 2)
        self.assertGreater(self.adapter.stats['dropped'], 0)
        await self.adapter.close()
        self.assertTrue(all(t.done() for t in self.adapter.tasks))
        self.assertEqual(self.adapter.stats['active'], 0)
        self.assertTrue(self.adapter.queue.empty())

    async def test_path_timeout_and_recovery(self):
        self.behavior = 'stall'
        self.send()
        await asyncio.sleep(.15)
        self.assertEqual(self.adapter.stats['errors'], 2)
        self.behavior = 'good'
        self.send(3)
        reply, _ = await asyncio.wait_for(self.received.get(), 1)
        self.assertEqual(reply[:2], b'\x00\x03')

    async def test_shard_worker_affinity_survives_retry(self):
        await self.adapter.close()
        self.adapter = device.Adapter(('127.0.0.1', '127.0.0.2'),
                                      self.server.sockets[0].getsockname()[1],
                                      workers=2, capacity=2, timeout=.05, age=.11)
        opened = []
        original = asyncio.open_connection
        async def connect(host, port, **kwargs):
            opened.append((asyncio.current_task(), host))
            return await original('127.0.0.1', port, **kwargs)
        self.behavior = 'stall'
        with patch.object(asyncio, 'open_connection', connect):
            self.transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                lambda: self.adapter, local_addr=('127.0.0.1', 0))
            self.send(1)
            self.send(2)
            await asyncio.sleep(.15)
        self.assertEqual(len(opened), 4)
        self.assertEqual({host for _, host in opened}, {'127.0.0.1', '127.0.0.2'})
        for task in self.adapter.tasks:
            self.assertEqual(len({host for owner, host in opened if owner is task}), 1)
        self.assertTrue(self.received.empty())

    async def test_bad_query_and_expiry(self):
        self.adapter.datagram_received(b'x', ('127.0.0.1', 100))
        self.adapter.datagram_received(bytes(12), ('192.0.2.1', 100))
        self.adapter.queue.put_nowait((bytes(12), ('127.0.0.1', 100), 0))
        await self.adapter.queue.join()
        self.assertEqual(self.adapter.stats['dropped'], 2)
        self.assertEqual(self.adapter.stats['expired'], 1)
        self.assertEqual(self.calls, 0)


class FixtureTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.cert = str(Path(self.tmp.name) / 'cert.pem')
        key = str(Path(self.tmp.name) / 'key.pem')
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1',
                        '-nodes', '-days', '1', '-subj', '/CN=smoke', '-addext', 'subjectAltName=IP:127.0.0.1',
                        '-keyout', key, '-out', self.cert], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(self.cert, key)
        self.fixture = Fixture('a' * 64, timeout=.2)
        self.server = await asyncio.start_server(self.fixture.handle, '127.0.0.1', 0,
                                                ssl=context, limit=8192)
        self.port = self.server.sockets[0].getsockname()[1]
        self.args = SimpleNamespace(fixture_host='127.0.0.1', fixture_port=self.port,
                                    fixture_cert=self.cert, deadline=1, egress='unchecked')

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()
        tasks = list(self.fixture.active)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        self.tmp.cleanup()

    async def test_exact_hash_ack_upload_and_download(self):
        with contextlib.redirect_stdout(io.StringIO()):
            for kind, size in [('up', 8192), ('up', 524288), ('down', 1048576)]:
                row = await device.request(self.args, {'fixture_token': 'a' * 64}, None, kind, size, 'test')
                self.assertTrue(row['ok'])
                self.assertEqual(row['acknowledged'], size)
        self.assertEqual(self.fixture.stats['tls_complete'], 3)
        self.assertEqual(self.fixture.stats['authenticated'], 3)
        self.assertEqual(self.fixture.stats['upload_complete'], 2)
        self.assertEqual(self.fixture.stats['reply_drained'], 3)
        self.assertEqual(self.fixture.stats['http_timeout'], 0)

    async def test_owned_runtime_cancellation_reaps_child_and_56_workers(self):
        create = asyncio.create_subprocess_exec
        adapter_type = device.Adapter
        paths = []
        def adapter(*args, **kwargs):
            path = adapter_type(*args, **kwargs)
            paths.append(path)
            return path
        children = []
        started = asyncio.Event()
        async def child(*argv, **kwargs):
            proc = await create(sys.executable, '-c',
                                'import sys,time; sys.stdin.buffer.read(); print("Connection ready",flush=True); time.sleep(60)',
                                **kwargs)
            children.append(proc)
            return proc
        async def wait(*args):
            started.set()
            await asyncio.sleep(60)
        args = SimpleNamespace(mode='native', order='accepted', topology='accepted', domains='legacy', connections=2, partition=True, workload='full', fixture_cert=self.cert, deadline=1,
                               egress='direct', native=sys.executable, domain='example.test', cert=self.cert)
        output = io.StringIO()
        with patch.object(asyncio, 'create_subprocess_exec', child), patch.object(device, 'Adapter', adapter), \
                patch.object(device, 'workload', wait), contextlib.redirect_stdout(output):
            task = asyncio.create_task(device.run(args, {'flow_token': 'a' * 32}))
            await asyncio.wait_for(started.wait(), 3)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        cleanup = json.loads(output.getvalue().splitlines()[-1])
        self.assertFalse(cleanup['child_alive'])
        self.assertEqual(cleanup['workers_alive'], 0)
        self.assertEqual(len(cleanup['paths']), 4)
        self.assertEqual(len(children), 2)
        self.assertEqual(sum(p.workers for p in paths), 56)
        self.assertEqual(sum(p.queue.maxsize for p in paths), 64)
        self.assertTrue(all(child.returncode is not None for child in children))

    async def test_short_parallel_workload_has_affinity_and_at_most_two_flows(self):
        active, maximum, calls = 0, 0, []
        async def request(args, secret, proxy, kind, size, condition):
            nonlocal active, maximum
            active += 1
            maximum = max(maximum, active)
            calls.append((proxy, kind, size, condition))
            await asyncio.sleep(.001)
            active -= 1
            return dict(ok=True, acknowledged=size)
        with patch.object(device, 'request', request), contextlib.redirect_stdout(io.StringIO()):
            rows = await device.workload(SimpleNamespace(workload='short-parallel'), {}, 'first', 'second')
        self.assertEqual(len(rows), 7)
        self.assertEqual(maximum, 2)
        self.assertEqual([p for p, _, _, c in calls if c.endswith('-b')], ['second', 'second'])
        self.assertEqual(sum(s for _, k, s, _ in calls if k == 'up'), 6 * 32768)

    async def test_unknown_auth_zero_ack_no_secret_output(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            row = await device.request(self.args, {'fixture_token': 'b' * 64}, None, 'up', 8192, 'test')
        self.assertFalse(row['ok'])
        self.assertEqual(row['ack_Bps'], 0)
        self.assertNotIn('b' * 64, output.getvalue())
        self.assertEqual(self.fixture.stats['tls_complete'], 1)
        self.assertEqual(self.fixture.stats['authenticated'], 0)
        self.assertEqual(self.fixture.stats['upload_complete'], 0)
        self.assertEqual(self.fixture.stats['reply_drained'], 0)

    async def test_causal_probe_cancellation_reaps_both_children_and_paths(self):
        create = asyncio.create_subprocess_exec
        children = []
        started = asyncio.Event()
        async def child(*argv, **kwargs):
            proc = await create(sys.executable, '-c',
                                'import time; print("Connection ready",flush=True); time.sleep(60)', **kwargs)
            children.append(proc)
            return proc
        async def wait(*args):
            started.set()
            await asyncio.sleep(60)
        args = SimpleNamespace(client=Path(sys.executable), server=Path(sys.executable),
                               delay=[.08, .4], focused=False, connections=2, cc='dcubic', partition=False, workload='full')
        output = io.StringIO()
        with patch.object(asyncio, 'create_subprocess_exec', child), patch.object(device, 'workload', wait), contextlib.redirect_stdout(output):
            task = asyncio.create_task(causal.window(args, Path(self.tmp.name), 1))
            await asyncio.wait_for(started.wait(), 3)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        cleanup = json.loads(output.getvalue().splitlines()[-1])
        self.assertFalse(cleanup['child_alive'])
        self.assertEqual(cleanup['tasks_alive'], 0)
        self.assertEqual(len(cleanup['paths']), 2)
        self.assertEqual(len(children), 3)
        self.assertTrue(all(child.returncode is not None for child in children))

    async def test_incomplete_body_not_counted_as_lost_ack(self):
        context = ssl.create_default_context(cafile=self.cert)
        reader, writer = await asyncio.open_connection('127.0.0.1', self.port, ssl=context)
        writer.write(b'POST /up HTTP/1.1\r\nContent-Length: 8192\r\nAuthorization: Bearer ' +
                     b'a' * 64 + b'\r\n\r\nx')
        await writer.drain()
        self.assertEqual(await asyncio.wait_for(reader.read(), 1), b'')
        await device.close_writer(writer)
        self.assertEqual(self.fixture.stats['authenticated'], 1)
        self.assertEqual(self.fixture.stats['http_timeout'], 1)
        self.assertEqual(self.fixture.stats['upload_complete'], 0)
        self.assertEqual(self.fixture.stats['reply_drained'], 0)

    async def test_malformed_and_slow_headers_bounded(self):
        context = ssl.create_default_context(cafile=self.cert)
        for header in [b'GET /down?bytes=99999999 HTTP/1.1\r\nAuthorization: Bearer ' + b'a' * 64 + b'\r\n\r\n',
                       b'POST /up HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\n', b'GET /']:
            reader, writer = await asyncio.open_connection('127.0.0.1', self.port, ssl=context)
            writer.write(header)
            await writer.drain()
            self.assertEqual(await asyncio.wait_for(reader.read(), 1), b'')
            await device.close_writer(writer)


if __name__ == '__main__':
    unittest.main()
