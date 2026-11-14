"""Bounded, synthetic Rust-client/C-server upload evidence; no native output."""
import argparse
import asyncio
import hashlib
import hmac
import json
import math
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time

from adapter import Adapter
from backend import close_writer
from run import socks_open

SIZES = (32768, 131072, 524288, 4194304)
CHUNK = b'x' * 16384


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class Fixture:
    """Fixed target only; Flow OPEN format from tests/test_runtime.py/backend.py."""
    def __init__(self, token, timeout=300):
        self.token = token
        self.timeout = timeout
        self.tasks = set()
        self.progress = {}
        self.errors = 0

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        try:
            async with asyncio.timeout(self.timeout):
                opening = await reader.readexactly(23)
                if (opening[0] != 0x20 or not hmac.compare_digest(opening[1:17], self.token)
                        or opening[17:] != b'\x7f\x00\x00\x01' + struct.pack('!H', 40100)):
                    raise ValueError('fixed Flow OPEN')
                header = await reader.readexactly(9)
                size = struct.unpack('!I', header[5:])[0]
                if header[:4] != b'AB01' or not 1 <= size <= max(SIZES):
                    raise ValueError('synthetic command')
                if header[4:5] == b'D':
                    while True:
                        writer.write(CHUNK)
                        await writer.drain()
                elif header[4:5] == b'U':
                    digest = hashlib.sha256()
                    received = 0
                    key = size
                    p = [0, time.monotonic(), 0., 0]
                    self.progress[key] = p
                    while received < size:
                        chunk = await reader.read(min(16384, size - received))
                        if not chunk:
                            raise EOFError()
                        if chunk != b'x' * len(chunk):
                            raise ValueError('synthetic payload')
                        now = time.monotonic()
                        gap = now - p[1]
                        p[2] = max(p[2], gap)
                        p[3] += int(gap >= 1.)
                        digest.update(chunk)
                        received += len(chunk)
                        p[0], p[1] = received, now
                    writer.write(b'OK' + digest.digest())
                    await writer.drain()
                else:
                    raise ValueError('synthetic opcode')
        except (ConnectionError, asyncio.IncompleteReadError, EOFError, TimeoutError):
            pass
        except ValueError:
            self.errors += 1
        finally:
            await close_writer(writer)
            self.tasks.discard(task)


class Observer(asyncio.DatagramProtocol):
    """Transparent loopback UDP hop; packet bytes retained only in memory."""
    def __init__(self, target, capacity=4096, age=5.):
        self.target, self.capacity, self.age = target, capacity, age
        self.pending = {}
        self.stats = dict(queries=0, replies=0, noerror=0, nxdomain=0, other_rcode=0,
                          empty_noerror=0, route_dropped=0, route_expired=0,
                          unmatched_reply=0, invalid_reply=0)

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, peer):
        if peer == self.target:
            if len(data) < 12 or not data[2] & 0x80:
                self.stats['invalid_reply'] += 1
                return
            destination = self.pending.pop(data[:2], None)
            if destination is None:
                self.stats['unmatched_reply'] += 1
                return
            self.count_reply(data)
            self.transport.sendto(data, destination[0])
        else:
            self.stats['queries'] += 1
            now = time.monotonic()
            expired = [key for key, (_, entered) in self.pending.items() if now-entered >= self.age]
            for key in expired:
                del self.pending[key]
            self.stats['route_expired'] += len(expired)
            prior = self.pending.get(data[:2])
            if len(data) < 12 or (prior and prior[0] != peer) or (not prior and len(self.pending) >= self.capacity):
                self.stats['route_dropped'] += 1
                return
            self.pending[data[:2]] = (peer, now)
            self.transport.sendto(data, self.target)

    def count_reply(self, data):
        self.stats['replies'] += 1
        rcode = data[3] & 15
        self.stats['noerror' if rcode == 0 else 'nxdomain' if rcode == 3 else 'other_rcode'] += 1
        if rcode == 0 and data[6:8] == b'\x00\x00':
            self.stats['empty_noerror'] += 1


class TCPModel:
    """Persistent framed TCP DNS to UDP origin, 100ms service delay per query."""
    def __init__(self, target, delay, counters=None):
        self.target, self.delay = target, delay
        self.tasks = set()
        self.counters = counters

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setblocking(False)
        sock.connect(self.target)
        loop = asyncio.get_running_loop()
        try:
            while True:
                size = struct.unpack('!H', await reader.readexactly(2))[0]
                if not 12 <= size <= 4096:
                    break
                data = await reader.readexactly(size)
                await asyncio.sleep(self.delay / 2)
                if self.counters:
                    self.counters.stats['queries'] += 1
                await loop.sock_sendall(sock, data)
                reply = await asyncio.wait_for(loop.sock_recv(sock, 4096), 2)
                if len(reply) < 12 or reply[:2] != data[:2] or not reply[2] & 0x80:
                    if self.counters:
                        self.counters.stats['invalid_reply'] += 1
                    raise ValueError('DNS reply identity')
                if self.counters:
                    self.counters.count_reply(reply)
                await asyncio.sleep(self.delay / 2)
                writer.write(struct.pack('!H', len(reply)) + reply)
                await writer.drain()
        except (OSError, ValueError, asyncio.IncompleteReadError, TimeoutError):
            pass
        finally:
            sock.close()
            await close_writer(writer)
            self.tasks.discard(task)


async def session(args, server_bin, label, model, pair, order, emit, cert, key):
    token = os.urandom(16)
    credentials = (os.urandom(16).hex(), os.urandom(16).hex())
    fixture = Fixture(token, max(args.deadline, args.sustained_deadline, args.warmup_deadline) + 30)
    backend = await asyncio.start_server(fixture.handle, '127.0.0.1', 0)
    dns = free_port(socket.SOCK_DGRAM)
    local = free_port()
    children, transports, servers, model_tasks = [], [], [backend], []
    adapter = None
    obs = Observer(('127.0.0.1', dns))
    loop = asyncio.get_running_loop()
    meta = dict(label=label, pair=pair, order=order, model=model,
                probe_version=4, client_kind=args.client_kind,
                congestion_control=args.congestion_control,
                campaign='rust-cc' if args.compare_cc else 'client' if args.compare_clients else 'server',
                server_sha256=sha(server_bin), client_sha256=sha(args.client))
    def spawn(binary, argv, secret=None):
        env = {k: v for k, v in os.environ.items()
               if k not in ('SSLKEYLOGFILE', 'QLOGDIR', 'QLOG_DIR', 'RUST_LOG')}
        child = subprocess.Popen([str(binary), *map(str, argv)], stdin=subprocess.PIPE,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        children.append(child)
        if secret:
            child.stdin.write(secret)
        child.stdin.close()
        return child
    try:
        spawn(server_bin, ['--dns-listen-host', '127.0.0.1', '--dns-listen-port', dns,
                          '--target-address', f'127.0.0.1:{backend.sockets[0].getsockname()[1]}',
                          '--cert', cert, '--key', key, '--domain', 't.test'])
        if model == 'tcp-delay-100ms':
            # Each worker's connected UDP socket owns its response. No shared
            # observer hop: counters aggregate only, DNS IDs remain unchanged.
            tcp = TCPModel(('127.0.0.1', dns), .1, obs)
            tcp_server = await asyncio.start_server(tcp.handle, '127.0.0.1', 0)
            servers.append(tcp_server)
            model_tasks = tcp.tasks
            adapter = Adapter('127.0.0.1', tcp_server.sockets[0].getsockname()[1], workers=56, capacity=64)
            transport, _ = await loop.create_datagram_endpoint(lambda: adapter, local_addr=('127.0.0.1', 0))
            transports.append(transport)
            resolver = transport.get_extra_info('sockname')[1]
        else:
            transport, _ = await loop.create_datagram_endpoint(lambda: obs, local_addr=('127.0.0.1', 0))
            transports.append(transport)
            resolver = transport.get_extra_info('sockname')[1]
        await asyncio.sleep(.15)
        spawn(args.client, ['--tcp-listen-host', '127.0.0.1', '--tcp-listen-port', local,
                            '--domain', 't.test', '--cert', cert, '--congestion-control', args.congestion_control,
                            '--flow-relay-stdin', '--authoritative' if args.client_kind == 'rust' else '--resolver', f'127.0.0.1:{resolver}'],
              token + ''.join(credentials).encode())
        startup = time.monotonic()
        while time.monotonic() - startup < 8:
            if any(c.poll() is not None for c in children):
                emit(dict(meta, kind='startup_failure', returncodes=[c.poll() for c in children]))
                return
            entries = Path('/proc/net/tcp').read_text().splitlines()[1:]
            if any(line.split()[1].endswith(f':{local:04X}') and line.split()[3] == '0A' for line in entries):
                break
            await asyncio.sleep(.05)
        else:
            emit(dict(meta, kind='startup_failure', reason='listener deadline'))
            return
        async def open_stream():
            return await socks_open(local, credentials, '127.0.0.1', 40100)
        warmup_start = time.monotonic()
        writer = None
        try:
            async with asyncio.timeout(args.warmup_deadline):
                reader, writer = await open_stream()
                writer.write(b'AB01U' + struct.pack('!I', 1024) + b'x' * 1024)
                await writer.drain()
                if await reader.readexactly(34) != b'OK' + hashlib.sha256(b'x'*1024).digest():
                    raise ValueError('warmup digest')
            emit(dict(meta, kind='warmup', acknowledged_bytes=1024,
                      elapsed_s=time.monotonic()-warmup_start))
        except (OSError, EOFError, ValueError, TimeoutError, asyncio.IncompleteReadError) as e:
            emit(dict(meta, kind='startup_failure', reason='acknowledged warmup', error=type(e).__name__))
            return
        finally:
            if writer:
                await close_writer(writer)
        fixture.progress.pop(1024, None)
        for loaded in (True,) if args.loaded_only else (False, True):
            for size in args.sizes:
                deadline = args.sustained_deadline if size == max(SIZES) else args.deadline
                if time.monotonic() + deadline + (args.warmup_deadline if loaded else 0) > args.end:
                    emit(dict(meta, kind='stop', reason='insufficient budget for full sample deadline'))
                    return
                down = dict(bytes=0, error=None)
                ready = asyncio.Event()
                async def download():
                    writer = None
                    try:
                        reader, writer = await open_stream()
                        writer.write(b'AB01D' + struct.pack('!I', max(SIZES)))
                        await writer.drain()
                        while True:
                            chunk = await reader.read(16384)
                            if not chunk:
                                raise EOFError()
                            if chunk != b'x' * len(chunk):
                                raise ValueError('download payload')
                            down['bytes'] += len(chunk)
                            ready.set()
                    except (OSError, EOFError, ValueError, asyncio.IncompleteReadError) as e:
                        down['error'] = type(e).__name__
                        ready.set()
                    finally:
                        if writer:
                            await close_writer(writer)
                task = asyncio.create_task(download()) if loaded else None
                if loaded:
                    try:
                        await asyncio.wait_for(ready.wait(), args.warmup_deadline)
                    except TimeoutError:
                        down['error'] = 'load_start_timeout'
                start = time.monotonic()
                down_start = down['bytes']
                before = dict(obs.stats)
                row = dict(meta, size=size, loaded=loaded, acknowledged_bytes=0,
                           submitted_bytes=0, deadline_s=deadline, error=None)
                writer = None
                interrupted = False
                try:
                    async with asyncio.timeout(deadline):
                        if loaded and (down['error'] or not down['bytes']):
                            raise ValueError('continuous load unavailable')
                        reader, writer = await open_stream()
                        writer.write(b'AB01U' + struct.pack('!I', size))
                        await writer.drain()
                        for offset in range(0, size, len(CHUNK)):
                            chunk = CHUNK[:min(len(CHUNK), size - offset)]
                            writer.write(chunk)
                            await writer.drain()
                            row['submitted_bytes'] += len(chunk)
                        ack = await reader.readexactly(34)
                        if ack != b'OK' + hashlib.sha256(b'x' * size).digest():
                            raise ValueError('SHA256 acknowledgement')
                        row['acknowledged_bytes'] = size
                except (OSError, EOFError, ValueError, TimeoutError, asyncio.IncompleteReadError) as e:
                    row['error'] = type(e).__name__
                except asyncio.CancelledError:
                    row['error'] = 'external_cancellation'
                    interrupted = True
                finally:
                    row['elapsed_s'] = time.monotonic() - start
                    down_measured = down['bytes'] - down_start
                    if writer:
                        await close_writer(writer)
                    if task:
                        task.cancel()
                        await asyncio.gather(task, return_exceptions=True)
                    # Closing the local SOCKS socket need not instantly abort
                    # its remote QUIC stream. Explicitly end all fixed-fixture
                    # streams before the next sample to prevent residual load.
                    pending = list(fixture.tasks)
                    for pending_task in pending:
                        pending_task.cancel()
                    await asyncio.gather(*pending, return_exceptions=True)
                p = fixture.progress.pop(size, [0, start, 0., 0])
                row.update(backend_received_bytes=p[0], max_receive_gap_s=max(p[2], time.monotonic()-p[1]) if p[0] < size else p[2],
                           receive_stalls_ge_1s=p[3], download_bytes=down_measured, download_error=down['error'],
                           dns={k: obs.stats[k]-before[k] for k in before},
                           adapter=dict(adapter.stats) if adapter else None, fixture_errors=fixture.errors,
                           native_alive=all(c.poll() is None for c in children))
                row['backend_observed_goodput_Bps'] = p[0] / row['elapsed_s']
                row['acknowledged_goodput_Bps'] = row['acknowledged_bytes'] / row['elapsed_s']
                row['receive_stalls_ge_1s'] += int(p[0] < size and time.monotonic()-p[1] >= 1.)
                emit(row)
                if interrupted:
                    raise asyncio.CancelledError()
        before = obs.stats['queries']
        await asyncio.sleep(1.)
        emit(dict(meta, idle_queries_1s=obs.stats['queries']-before, kind='idle'))
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
            try:
                await asyncio.to_thread(child.wait, 2)
            except subprocess.TimeoutExpired:
                child.kill()
                await asyncio.to_thread(child.wait)
        if adapter:
            await adapter.close()
        for transport in transports:
            transport.close()
        for server in servers:
            server.close()
            await server.wait_closed()
        tasks = list(fixture.tasks | set(model_tasks))
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


async def main(args):
    current = asyncio.current_task()
    for signum in (signal.SIGINT, signal.SIGTERM):
        asyncio.get_running_loop().add_signal_handler(signum, current.cancel)
    args.end = time.monotonic() + args.budget
    args.client = Path(args.client).resolve()
    baseline = Path(args.baseline).resolve()
    candidate = Path(args.candidate).resolve()
    rows = []
    fd = os.open(args.output, os.O_APPEND | os.O_CREAT | os.O_WRONLY, 0o600)
    temp_root = Path(__file__).resolve().parents[1] / '.cache'
    temp_root.mkdir(mode=0o700, exist_ok=True)
    with os.fdopen(fd, 'a') as output, tempfile.TemporaryDirectory(prefix='upload-first-', dir=temp_root) as tmp:
        def emit(row):
            rows.append(row)
            output.write(json.dumps(row, allow_nan=False) + '\n')
            output.flush()
            if 'size' in row:
                print(row['label'], row['pair'], row['model'], row['size'], row['loaded'],
                      round(row['elapsed_s'], 3), row['error'], flush=True)
        cert, key = Path(tmp)/'cert.pem', Path(tmp)/'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=t.test', '-keyout', str(key), '-out', str(cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if args.compare_clients or args.compare_cc:
            # Fixed server, fresh processes per arm; no native source changes.
            rust = args.client
            cclient = rust if args.compare_cc else Path(args.c_client).resolve()
            expected = {path: sha(path) for path in (baseline, rust, cclient)}
            async def arm(label, pair, order, sizes, loaded_only=False):
                if time.monotonic() >= args.end or any(sha(path) != digest for path, digest in expected.items()):
                    emit(dict(kind='stop', reason='client comparison budget or binary mutation', pair=pair, label=label))
                    return False
                run = argparse.Namespace(**vars(args))
                run.client = rust if label == 'A' else cclient
                run.client_kind = 'rust' if args.compare_cc or label == 'A' else 'c'
                if args.compare_cc:
                    run.congestion_control = 'dcubic' if label == 'A' else 'bbr'
                run.sizes, run.loaded_only = sizes, loaded_only
                await session(run, baseline, label, 'tcp-delay-100ms', pair, order, emit, cert, key)
                return True
            for pair in range(2):
                for order, label in enumerate(('A', 'B') if pair == 0 else ('B', 'A')):
                    if not await arm(label, f'screen-{pair+1}', order, [524288]):
                        return
            screen = [r for r in rows if 'size' in r and r['pair'].startswith('screen')]
            gains, download_ratios = [], []
            healthy = len(screen) == 8 and all(not r['error'] and not r['download_error']
                        and not r['fixture_errors'] and r['native_alive'] for r in screen)
            for pair in ('screen-1', 'screen-2'):
                group = {r['label']: r for r in screen if r['pair'] == pair and r['loaded']}
                if set(group) == {'A', 'B'}:
                    gains.append(1 - group['B']['elapsed_s'] / group['A']['elapsed_s'])
                    rate_a = group['A']['download_bytes'] / group['A']['elapsed_s']
                    rate_b = group['B']['download_bytes'] / group['B']['elapsed_s']
                    download_ratios.append(rate_b / rate_a if rate_a else 0.)
            median_gain = sum(gains)/2 if len(gains) == 2 else None
            confirm = healthy and median_gain is not None and median_gain >= .2
            if args.compare_cc:
                confirm = confirm and len(download_ratios) == 2 and min(download_ratios) >= .7
            emit(dict(kind='cc_gate' if args.compare_cc else 'client_gate', confirmation=confirm, loaded_paired_time_gains=gains,
                      loaded_paired_median_gain=median_gain, failure_free_screen=healthy,
                      loaded_paired_download_rate_ratios=download_ratios,
                      fixed_server_sha256=expected[baseline]))
            if args.compare_clients or confirm:
                for order, label in enumerate(('A', 'B')):
                    if not await arm(label, 'sustained-1', order, [4194304]):
                        return
            if confirm:
                for pair in range(6):
                    for order, label in enumerate(('A', 'B') if pair % 2 == 0 else ('B', 'A')):
                        if not await arm(label, f'confirm-{pair+1}', order, [524288], True):
                            return
            return
        if args.single_label:
            for model in [args.single_model] if args.single_model else args.models:
                await session(args, baseline if args.single_label == 'A' else candidate,
                              args.single_label, model, args.single_pair,
                              args.single_order, emit, cert, key)
            return
        for pair in range(2):
            for model in args.models:
                await session(args, baseline, 'A', model, f'AA-{pair+1}', pair, emit, cert, key)
        if args.baseline_only:
            return
        while (not candidate.exists() or sha(candidate) == sha(baseline)) and time.monotonic() < args.end-30:
            await asyncio.sleep(2)
        if not candidate.exists() or sha(candidate) == sha(baseline):
            emit(dict(kind='stop', reason='distinct candidate unavailable'))
            return
        expected = sha(candidate)
        for pair in range(2):
            for model in args.models:
                for order, label in enumerate(('A','B') if pair == 0 else ('B','A')):
                    if time.monotonic() >= args.end or sha(candidate) != expected:
                        emit(dict(kind='stop', reason='budget or candidate mutation'))
                        return
                    await session(args, baseline if label == 'A' else candidate, label, model,
                                  f'screen-{pair+1}', order, emit, cert, key)
        # Candidate correctness alone does not trigger six confirmation pairs.
        gains = []
        for model in args.models:
            a = [r for r in rows if r.get('label') == 'A' and r.get('loaded') and r.get('model') == model and str(r.get('pair')).startswith('screen')]
            b = [r for r in rows if r.get('label') == 'B' and r.get('loaded') and r.get('model') == model]
            if len(a) == len(b) == 2 * len(args.sizes) and all(not r['error'] for r in a+b):
                gains.append(1 - sum(r['elapsed_s'] for r in b)/sum(r['elapsed_s'] for r in a))
        screens = [r for r in rows if 'size' in r and str(r.get('pair')).startswith('screen')]
        no_new_failure = len(screens) == 8 * len(args.sizes) * len(args.models) and all(not b['error'] or next(a['error'] for a in screens
                              if a['label'] == 'A' and a['pair'] == b['pair']
                              and a['model'] == b['model'] and a['loaded'] == b['loaded']
                              and a['size'] == b['size']) for b in screens if b['label'] == 'B')
        if gains and max(gains) >= .2 and no_new_failure:
            for pair in range(6):
                for model in args.models:
                    for order, label in enumerate(('A','B') if pair % 2 == 0 else ('B','A')):
                        if time.monotonic() >= args.end or sha(candidate) != expected:
                            emit(dict(kind='stop', reason='confirmation budget or candidate mutation'))
                            return
                        await session(args, baseline if label == 'A' else candidate, label, model,
                                      f'confirm-{pair+1}', order, emit, cert, key)
        else:
            emit(dict(kind='gate', confirmation=False, loaded_time_gains=gains,
                      reason='no failure-free >=20% loaded aggregate gain'))


def summarize(paths):
    """Last row wins on repeated arm; useful for explicitly replaced partial runs."""
    rows = {}
    idle = []
    events = []
    for path in paths:
        for line in Path(path).read_text().splitlines():
            row = json.loads(line)
            if 'size' in row:
                rows[(row['pair'], row['label'], row['model'], row['loaded'], row['size'])] = row
            elif row.get('kind') == 'idle':
                idle.append(row)
            else:
                events.append(row)
    def pct(values, p):
        return sorted(values)[max(0, math.ceil(len(values)*p)-1)]
    print(json.dumps(dict(hashes=sorted({(r['label'], r['server_sha256'], r['client_sha256']) for r in rows.values()}),
                          samples=len(rows), idle=idle, events=events), indent=2))
    for model in ('udp-loopback', 'tcp-delay-100ms'):
        for phase in ('AA', 'screen', 'confirm', 'sustained'):
            for label in ('A', 'B'):
                for loaded in (False, True):
                    group = [r for r in rows.values() if r['model'] == model and r['pair'].startswith(phase)
                             and r['label'] == label and r['loaded'] == loaded]
                    if not group:
                        continue
                    result = dict(model=model, phase=phase, label=label, loaded=loaded,
                                  n=len(group), failures=sum(bool(r['error']) for r in group),
                                  elapsed_sum=sum(r['elapsed_s'] for r in group),
                                  elapsed_p95_s=pct([r['elapsed_s'] for r in group], .95),
                                  gap_p95=pct([r['max_receive_gap_s'] for r in group], .95),
                                  stalls=sum(r['receive_stalls_ge_1s'] for r in group),
                                  dns={k: sum(r['dns'][k] for r in group) for k in group[0]['dns']},
                                  per_size={str(size): dict(seconds=[round(r['elapsed_s'], 4) for r in group if r['size']==size],
                                                           ack=[r['acknowledged_bytes'] for r in group if r['size']==size],
                                                           received=[r['backend_received_bytes'] for r in group if r['size']==size],
                                                           download=[r['download_bytes'] for r in group if r['size']==size]) for size in SIZES},
                                  adapter_dropped_max=max((r['adapter']['dropped'] if r['adapter'] else 0) for r in group),
                                  adapter_errors_max=max((r['adapter']['errors'] if r['adapter'] else 0) for r in group),
                                  adapter_retries_max=max((r['adapter']['retries'] if r['adapter'] else 0) for r in group),
                                  adapter_active_max=max((r['adapter']['active_max'] if r['adapter'] else 0) for r in group),
                                  adapter_queue_max=max((r['adapter']['queue_max'] if r['adapter'] else 0) for r in group),
                                  fixture_errors=max(r['fixture_errors'] for r in group),
                                  native_alive=all(r['native_alive'] for r in group),
                                  download_errors=sum(bool(r['download_error']) for r in group))
                    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', default='../slipstream-rust-control-artifacts/slipstream-client')
    parser.add_argument('--client-kind', choices=('rust', 'c'), default='rust')
    parser.add_argument('--congestion-control', choices=('dcubic', 'bbr'), default='dcubic')
    parser.add_argument('--compare-cc', action='store_true', help='Fixed Rust client/server, DCUBIC A vs BBR B; gated loaded confirmations')
    parser.add_argument('--compare-clients', action='store_true', help='Rust A vs C B on fixed --baseline server; 512KiB AB/BA, 4MiB pair, gated loaded confirmations')
    parser.add_argument('--c-client', default='build-candidate/slipstream-client')
    parser.add_argument('--loaded-only', action='store_true')
    parser.add_argument('--baseline', default='build-upload-baseline/slipstream-server')
    parser.add_argument('--candidate', default='build-candidate/slipstream-server')
    parser.add_argument('--output', default='bench/upload-first.jsonl')
    parser.add_argument('--deadline', type=float, default=60.)
    parser.add_argument('--sustained-deadline', type=float, default=180.)
    parser.add_argument('--warmup-deadline', type=float, default=30.)
    parser.add_argument('--sizes', type=int, nargs='+', default=list(SIZES))
    parser.add_argument('--models', nargs='+', choices=('udp-loopback', 'tcp-delay-100ms'), default=['udp-loopback', 'tcp-delay-100ms'])
    parser.add_argument('--budget', type=float, default=1800.)
    parser.add_argument('--baseline-only', action='store_true')
    parser.add_argument('--single-label', choices=('A', 'B'))
    parser.add_argument('--single-model', choices=('udp-loopback', 'tcp-delay-100ms'))
    parser.add_argument('--single-pair', default='screen-2')
    parser.add_argument('--single-order', type=int, default=1)
    parser.add_argument('--summarize', nargs='+', metavar='JSONL')
    args = parser.parse_args()
    if args.compare_cc and (args.compare_clients or args.client_kind != 'rust' or args.single_label):
        parser.error('--compare-cc requires Rust and cannot combine other comparison/single-arm modes')
    if (not 1 <= args.deadline <= 300 or not 1 <= args.sustained_deadline <= 600
            or not 1 <= args.warmup_deadline <= 120 or not 1 <= args.budget <= 7200
            or any(size not in SIZES for size in args.sizes)):
        parser.error('deadline/budget outside bounds')
    if args.summarize:
        summarize(args.summarize)
    else:
        try:
            asyncio.run(main(args))
        except (asyncio.CancelledError, KeyboardInterrupt):
            raise SystemExit(130)
