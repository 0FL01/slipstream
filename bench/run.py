"""Local Linux Slipstream AB runner. Server switching is always external."""
import argparse
import asyncio
import hashlib
import ipaddress
import json
import math
import os
from pathlib import Path
import platform
import re
import secrets
import signal
import socket
import ssl
import stat
import struct
import subprocess
import sys
import time

from adapter import Adapter
from backend import close_writer

ROOT = Path(__file__).resolve().parents[2]
BINARIES = {'C0': ROOT / 'slipstream-c-baseline/build/slipstream-client',
            'C1': ROOT / 'slipstream-c/build-candidate/slipstream-client',
            'R': ROOT / 'slipstream-rust-control-artifacts/slipstream-client'}
SIZES = (8192, 32768, 131072, 524288)
LABEL = re.compile(r'^[A-Za-z0-9_.-]{1,80}$')


def protected_json(path):
    if path == '-':
        raw = sys.stdin.buffer.readline(8193)
    else:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, 'rb') as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077 or info.st_uid != os.getuid():
                raise ValueError('protected file required')
            raw = source.read(8193)
    if len(raw) > 8192:
        raise ValueError('credential bound')
    secret = json.loads(raw)
    if not isinstance(secret, dict):
        raise ValueError('credential object')
    for key, length in (('flow_token', 16), ('fixture_token', 32)):
        if key in secret and (not isinstance(secret[key], str) or
                              len(secret[key]) != length * 2 or
                              len(bytes.fromhex(secret[key])) != length):
            raise ValueError('credential length')
    return secret


class Results:
    def __init__(self, path, metadata):
        fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        self.output = os.fdopen(fd, 'a')
        self.metadata = metadata

    def emit(self, row):
        self.output.write(json.dumps(dict(self.metadata, **row), allow_nan=False) + '\n')
        self.output.flush()


def resources(pid):
    try:
        data = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return dict(cpu_s=(int(data[11]) + int(data[12])) / os.sysconf('SC_CLK_TCK'),
                    rss_bytes=int(data[21]) * os.sysconf('SC_PAGE_SIZE'), threads=int(data[17]))
    except (OSError, ValueError, IndexError):
        return None


def resource_delta(before, after):
    if before is None or after is None:
        return None
    return {key: after[key] - before[key] for key in before}


def percentile(values, fraction):
    if not values:
        return None
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


async def socks_open(port, credentials, host, target_port):
    # Same RFC1929/CONNECT protocol as Owenclave smoke/device.py.
    reader, writer = await asyncio.open_connection('127.0.0.1', port)
    try:
        user, password = (value.encode('ascii') for value in credentials)
        writer.write(b'\x05\x01\x02')
        await writer.drain()
        if await reader.readexactly(2) != b'\x05\x02':
            raise ValueError('SOCKS method')
        writer.write(b'\x01' + bytes([len(user)]) + user + bytes([len(password)]) + password)
        await writer.drain()
        if await reader.readexactly(2) != b'\x01\x00':
            raise ValueError('SOCKS auth')
        address = ipaddress.ip_address(host)
        writer.write(b'\x05\x01\x00' + bytes([1 if address.version == 4 else 4]) +
                     address.packed + struct.pack('!H', target_port))
        await writer.drain()
        header = await reader.readexactly(4)
        if header[:3] != b'\x05\x00\x00' or header[3] not in (1, 3, 4):
            raise ValueError('SOCKS connect')
        size = (await reader.readexactly(1))[0] if header[3] == 3 else {1: 4, 4: 16}[header[3]]
        await reader.readexactly(size + 2)
        return reader, writer
    except BaseException:
        await close_writer(writer)
        raise


class Runner:
    def __init__(self, args, secret, output):
        self.args, self.secret, self.output = args, secret, output
        self.paths, self.child = [], None
        self.container = None
        self.pid = None
        self.rows = []
        self.local_credentials = (secrets.token_hex(16), secrets.token_hex(16))
        self.port = None
        self.monitor_task = None
        self.peaks = dict(rss_bytes=0, threads=0)

    async def monitor(self):
        while True:
            sample = resources(self.pid) if self.pid else None
            if sample:
                for key in self.peaks:
                    self.peaks[key] = max(self.peaks[key], sample[key])
            await asyncio.sleep(.2)

    async def open(self):
        a = self.args
        if a.variant != 'C0':
            return await socks_open(self.port, self.local_credentials, a.target_host, a.target_port)
        reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
        try:
            if a.baseline_flow_open:
                address = ipaddress.ip_address(a.target_host)
                writer.write(bytes([0x20 if address.version == 4 else 0x22]) +
                             bytes.fromhex(self.secret['flow_token']) + address.packed + struct.pack('!H', a.target_port))
                await writer.drain()
            return reader, writer
        except BaseException:
            await close_writer(writer)
            raise

    async def transport(self, reader, writer, opcode, size, row):
        digest = hashlib.sha256(b'x' * size).digest()
        writer.write(b'AB01' + opcode.encode() + struct.pack('!I', size))
        await writer.drain()
        if opcode in ('U', 'E'):
            for offset in range(0, size, 4096):
                chunk = b'x' * min(4096, size - offset)
                writer.write(chunk)
                await writer.drain()
                row['submitted'] += len(chunk)
            # FIN independence correctness check on uploads/echo. Download must
            # keep write direction open until the received digest is sent.
            if self.args.half_close:
                writer.write_eof()
        if opcode in ('D', 'E'):
            first = await reader.readexactly(1)
            row['first_byte_s'] = time.monotonic() - row.pop('_start')
            body = first + await reader.readexactly(size - 1)
            if body != b'x' * size:
                raise ValueError('payload mismatch')
            if opcode == 'D':
                writer.write(digest)
                await writer.drain()
                if self.args.half_close:
                    writer.write_eof()
        if opcode != 'E' and await reader.readexactly(34) != b'OK' + digest:
            raise ValueError('remote digest acknowledgement')

    async def https(self, reader, writer, opcode, size, row):
        # Existing smoke fixture's authenticated HTTP contract, pinned inner TLS.
        context = ssl.create_default_context(cafile=self.args.fixture_cert)
        await writer.start_tls(context, server_hostname=self.args.target_host,
                               ssl_handshake_timeout=self.args.deadline)
        method, path = ('POST', '/up') if opcode == 'U' else ('GET', '/down?bytes=' + str(size))
        headers = (f'{method} {path} HTTP/1.1\r\nHost: {self.args.target_host}\r\n'
                   f'Authorization: Bearer {self.secret["fixture_token"]}\r\n'
                   f'Content-Length: {size if opcode == "U" else 0}\r\nConnection: close\r\n\r\n')
        writer.write(headers.encode('ascii'))
        await writer.drain()
        if opcode == 'U':
            for offset in range(0, size, 4096):
                chunk = b'x' * min(4096, size - offset)
                writer.write(chunk)
                await writer.drain()
                row['submitted'] += len(chunk)
        first = await reader.readexactly(1)
        row['first_byte_s'] = time.monotonic() - row.pop('_start')
        header = first + await reader.readuntil(b'\r\n\r\n')
        if len(header) > 8192:
            raise ValueError('HTTP bound')
        lines = header.decode('ascii').split('\r\n')
        fields = dict(line.lower().split(': ', 1) for line in lines[1:] if ': ' in line)
        length = int(fields['content-length'])
        if lines[0].split()[1] != '200' or not 0 <= length <= (1024 if opcode == 'U' else size):
            raise ValueError('fixture response')
        if fields.get('x-fixture-version') != '2':
            raise ValueError('fixture version')
        if self.args.egress != 'unchecked' and fields.get('x-egress-direct') != ('1' if self.args.egress == 'direct' else '0'):
            raise ValueError('fixture egress')
        body = await reader.readexactly(length)
        if opcode == 'U':
            if json.loads(body) != {'bytes': size, 'sha256': hashlib.sha256(b'x' * size).hexdigest()}:
                raise ValueError('fixture ACK')
        elif length != size or fields['x-sha256'] != hashlib.sha256(body).hexdigest():
            raise ValueError('fixture digest')

    async def request(self, opcode, size, condition, emit=True):
        started = time.monotonic()
        row = dict(kind='request', opcode=opcode, condition=condition, requested=size,
                   submitted=0, acknowledged=0, ok=False, timeout=False,
                   connect_s=None, first_byte_s=None, _start=started)
        writer = None
        try:
            async with asyncio.timeout(self.args.deadline):
                reader, writer = await self.open()
                row['connect_s'] = time.monotonic() - started
                writer.transport.set_write_buffer_limits(high=16384, low=4096)
                if self.args.backend == 'transport':
                    await self.transport(reader, writer, opcode, size, row)
                else:
                    await self.https(reader, writer, opcode, size, row)
                row.update(ok=True, acknowledged=size)
        except asyncio.CancelledError:
            row['error'] = 'CancelledError'
            raise
        except (OSError, EOFError, ValueError, KeyError, TimeoutError, asyncio.LimitOverrunError) as error:
            row['error'] = type(error).__name__
            row['timeout'] = isinstance(error, TimeoutError)
        finally:
            row.pop('_start', None)
            row['seconds'] = time.monotonic() - started
            row['ack_Bps'] = row['acknowledged'] / row['seconds']
            if writer:
                await close_writer(writer)
            if emit:
                self.rows.append(row)
                self.output.emit(row)
        return row

    def snapshot(self):
        return dict(resources=resources(self.pid) if self.pid else None,
                    adapter=[dict(path.stats) for path in self.paths])

    async def idle(self, seconds, condition):
        before = self.snapshot()
        start = time.monotonic()
        await asyncio.sleep(seconds)
        after = self.snapshot()
        elapsed = time.monotonic() - start
        counts = [{key: end[key] - begin[key] for key in begin if key not in
                   ('active', 'active_max', 'queue_max')} for begin, end in zip(before['adapter'], after['adapter'])]
        self.output.emit(dict(kind='idle', condition=condition, seconds=elapsed,
                              adapter_delta=counts, queries_per_s=sum(p['received'] for p in counts) / elapsed
                              if self.paths else None, resources_before=before['resources'],
                              resources_after=after['resources'],
                              resource_delta=resource_delta(before['resources'], after['resources'])))

    async def start(self):
        a = self.args
        endpoints = []
        layout = [] if a.carrier == 'direct' else [('77.88.8.88', 56, 64)] if a.carrier == 'recursive' else [
            ('77.88.8.88', 28, 32), ('77.88.8.1', 28, 32)]
        for host, workers, capacity in layout:
            path = Adapter(host, workers=workers, capacity=capacity)
            transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                lambda: path, local_addr=('127.0.0.1', 0))
            self.paths.append(path)
            endpoints.append('127.0.0.1:' + str(transport.get_extra_info('sockname')[1]))
        if not endpoints:
            endpoints = [a.direct_dns]
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            self.port = sock.getsockname()[1]
        executable = Path(a.binary or BINARIES[a.variant]).resolve()
        cert = Path(a.cert).resolve() if a.cert else None
        argv = ['--tcp-listen-port', str(self.port), '--domain', a.domain, '--congestion-control', 'dcubic']
        if a.variant != 'C0':
            argv += ['--tcp-listen-host', '127.0.0.1', '--cert', str(cert), '--flow-relay-stdin']
        for endpoint in endpoints:
            # Rust treats our UDP loopback TCP adapters as authoritative paths;
            # C has a single repeated --resolver option for either endpoint kind.
            argv += ['--authoritative' if a.variant == 'R' else '--resolver', endpoint]
        mode = a.execution
        if mode == 'auto':
            try:
                check = subprocess.run([str(executable), '--help'], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, timeout=5)
                mode = 'native' if check.returncode == 0 else 'docker'
            except (OSError, subprocess.TimeoutExpired):
                mode = 'docker'
        command = [str(executable), *argv]
        if mode == 'docker':
            self.container = 'slipstream-ab-' + secrets.token_hex(8)
            command = ['docker', 'run', '--rm', '-i', '--network', 'host', '--pid', 'host',
                       '--log-driver', 'none', '--name', self.container,
                       '-v', str(executable.parent) + ':/artifacts:ro']
            if cert:
                command += ['-v', str(cert) + ':/carrier-cert.pem:ro']
                argv[argv.index('--cert') + 1] = '/carrier-cert.pem'
            command += ['--entrypoint', '/artifacts/' + executable.name, a.image, *argv]
        env = {key: value for key, value in os.environ.items()
                if key not in ('SSLKEYLOGFILE', 'QLOGDIR', 'QLOG_DIR', 'RUST_LOG')}
        if a.variant == 'C0':
            shim = Path(__file__).with_name('bind_local.so').resolve()
            if not shim.is_file() or mode != 'native':
                raise ValueError('C0 requires native loopback binding adapter')
            env['LD_PRELOAD'] = str(shim)
        self.child = await asyncio.create_subprocess_exec(*command, stdin=asyncio.subprocess.PIPE,
                          stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL, env=env)
        if a.variant != 'C0':
            self.child.stdin.write(bytes.fromhex(self.secret['flow_token']) +
                                   ''.join(self.local_credentials).encode('ascii'))
            await self.child.stdin.drain()
        self.child.stdin.close()
        self.pid = self.child.pid if mode == 'native' else None
        self.output.emit(dict(kind='runtime', execution=mode))
        started = time.monotonic()
        ready = False
        async with asyncio.timeout(a.startup_deadline):
            while self.child.returncode is None:
                try:
                    # Listener-only check opens no application stream: Linux /proc
                    # supplies readiness without creating poll-inducing probes.
                    entries = Path('/proc/net/tcp').read_text().splitlines()[1:]
                    if any(line.split()[1].endswith(f':{self.port:04X}') and line.split()[3] == '0A' for line in entries):
                        if self.container:
                            inspect = await asyncio.create_subprocess_exec('docker', 'inspect', '--format',
                                '{{.State.Pid}}', self.container, stdout=asyncio.subprocess.PIPE,
                                stderr=asyncio.subprocess.DEVNULL)
                            raw, _ = await inspect.communicate()
                            self.pid = int(raw.strip()) if inspect.returncode == 0 else None
                        probe = await self.request('E' if a.backend == 'transport' else 'U', 32, 'startup-probe', emit=False)
                        ready = probe['ok']
                        if ready:
                            break
                except (OSError, ValueError):
                    pass
                await asyncio.sleep(.1)
        if not ready:
            raise ValueError('client startup')
        self.output.emit(dict(kind='startup', ok=True, seconds=time.monotonic() - started))
        self.monitor_task = asyncio.create_task(self.monitor())

    async def workload(self):
        a = self.args
        before = self.snapshot()
        start = time.monotonic()
        for size in SIZES:
            await self.request('U', size, 'sequential')
        await self.request('D', 1048576, 'sequential')
        group_start = time.monotonic()
        pair = await asyncio.gather(self.request('D', 1048576, 'two-flows'),
                                    self.request('D', 1048576, 'two-flows'))
        elapsed = time.monotonic() - group_start
        self.output.emit(dict(kind='parallel_group', seconds=elapsed, ok=all(row['ok'] for row in pair),
                              acknowledged=sum(row['acknowledged'] for row in pair),
                              ack_Bps=sum(row['acknowledged'] for row in pair) / elapsed))
        for size in SIZES:
            stop = asyncio.Event()
            async def load():
                while not stop.is_set():
                    row = await self.request('D', 1048576, 'background')
                    if not row['ok']:
                        await asyncio.sleep(.1)
            task = asyncio.create_task(load())
            try:
                await asyncio.sleep(.25)
                await self.request('U', size, 'loaded')
            finally:
                stop.set()
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
        # Empty idle 5s; quiet open stream 15s; recovery and empty settle 5s.
        await self.idle(5, 'empty')
        reader = writer = None
        try:
            async with asyncio.timeout(a.deadline):
                reader, writer = await self.open()
            await self.idle(15, 'quiet-open')
        except (OSError, EOFError, ValueError, TimeoutError) as error:
            self.output.emit(dict(kind='idle', condition='quiet-open', ok=False, error=type(error).__name__))
        finally:
            if writer:
                await close_writer(writer)
        await self.request('U', 8192, 'recovery')
        await self.idle(5, 'settle')
        after = self.snapshot()
        measured = [row for row in self.rows if row['condition'] != 'background']
        background = [row for row in self.rows if row['condition'] == 'background']
        latencies = [row['seconds'] for row in measured if row['ok']]
        loaded = [row['seconds'] for row in measured if row['condition'] == 'loaded' and row['ok']]
        background_failed = any(not row['ok'] and row.get('error') != 'CancelledError' for row in background)
        self.output.emit(dict(kind='window', ok=all(row['ok'] for row in measured) and not background_failed,
                              failures=sum(not row['ok'] for row in measured),
                              timeouts=sum(row['timeout'] for row in measured),
                              background_failures=sum(not row['ok'] and row.get('error') != 'CancelledError' for row in background),
                              background_cancelled=sum(row.get('error') == 'CancelledError' for row in background),
                              seconds=time.monotonic() - start, latency_p50_s=percentile(latencies, .5),
                              latency_p95_s=percentile(latencies, .95), latency_max_s=max(latencies, default=None),
                              loaded_p95_s=percentile(loaded, .95), resources_before=before['resources'],
                              resources_after=after['resources'],
                              resource_delta=resource_delta(before['resources'], after['resources']),
                              resource_peaks=self.peaks, adapter=after['adapter']))
        return all(row['ok'] for row in measured) and not background_failed

    async def cleanup(self):
        if self.monitor_task:
            self.monitor_task.cancel()
            await asyncio.gather(self.monitor_task, return_exceptions=True)
        if self.container:
            stop = await asyncio.create_subprocess_exec('docker', 'rm', '-f', self.container,
                        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
            await stop.wait()
        if self.child and self.child.returncode is None:
            self.child.terminate()
            try:
                await asyncio.wait_for(self.child.wait(), 3)
            except TimeoutError:
                self.child.kill()
                await self.child.wait()
        for path in self.paths:
            await path.close()
        self.output.emit(dict(kind='cleanup', child_alive=bool(self.child and self.child.returncode is None),
                              workers_alive=sum(not task.done() for path in self.paths for task in path.tasks),
                              adapter=[path.stats for path in self.paths]))


def schedule(phase):
    if phase == 'aa':
        return [(0, 0, 'A'), (0, 1, 'A')]
    count = 2 if phase == 'screen' else 6
    return [(pair, position, label) for pair in range(count)
            for position, label in enumerate(('A', 'B') if pair % 2 == 0 else ('B', 'A'))]


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=BINARIES)
    parser.add_argument('--server', help='Secret-free server variant/build label; metadata only')
    parser.add_argument('--results')
    parser.add_argument('--credentials', help='Owner-only JSON file or - for stdin; never argv credential values')
    parser.add_argument('--cert')
    parser.add_argument('--binary')
    parser.add_argument('--domain', default='t.x.ass-peak.de')
    parser.add_argument('--carrier', choices=('direct', 'recursive', 'multipath'), default='direct')
    parser.add_argument('--direct-dns', default='195.128.101.186:53')
    parser.add_argument('--backend', choices=('transport', 'https'), default='transport')
    parser.add_argument('--target-host', default='127.0.0.1')
    parser.add_argument('--target-port', type=int, default=40100)
    parser.add_argument('--fixture-cert')
    parser.add_argument('--egress', choices=('direct', 'warp', 'unchecked'), default='unchecked')
    parser.add_argument('--baseline-flow-open', action='store_true')
    parser.add_argument('--half-close', action='store_true', help='Explicit independent-FIN correctness screen')
    parser.add_argument('--execution', choices=('auto', 'native', 'docker'), default='auto')
    parser.add_argument('--image', default='slipstream-ab-build:bookworm')
    parser.add_argument('--deadline', type=float, default=60)
    parser.add_argument('--startup-deadline', type=float, default=15)
    parser.add_argument('--phase', choices=('aa', 'screen', 'confirm'), default='screen')
    parser.add_argument('--campaign', default='ab')
    parser.add_argument('--pair', type=int, default=0)
    parser.add_argument('--position', type=int, choices=(0, 1), default=0)
    parser.add_argument('--arm', choices=('A', 'B'), default='A')
    parser.add_argument('--sample', type=int, default=0)
    parser.add_argument('--samples', type=int, choices=(1, 2), default=1)
    parser.add_argument('--schedule', action='store_true', help='Print frozen order only; no process/server actions')
    args = parser.parse_args(argv)
    if args.schedule:
        return args
    for name in ('variant', 'server', 'results'):
        if not getattr(args, name):
            parser.error('--' + name + ' required')
    if not LABEL.fullmatch(args.server) or not LABEL.fullmatch(args.campaign):
        parser.error('server/campaign must be bounded secret-free labels')
    if args.variant != 'C0' and not (args.credentials and args.cert):
        parser.error('C1/R require --credentials and --cert')
    if args.baseline_flow_open and not args.credentials:
        parser.error('Flow OPEN requires --credentials')
    if args.backend == 'https' and not (args.fixture_cert and args.credentials):
        parser.error('HTTPS fixture requires --fixture-cert and --credentials')
    if args.half_close and args.backend != 'transport':
        parser.error('half-close requires AB01 transport fixture')
    if not 1 <= args.target_port <= 65535 or not 1 <= args.deadline <= 300 or not 1 <= args.startup_deadline <= 60:
        parser.error('port/deadline outside bounds')
    if args.pair < 0 or args.sample < 0 or (args.samples == 2 and args.phase != 'aa'):
        parser.error('sample/pair bounds; two samples are A/A only')
    try:
        ipaddress.ip_address(args.target_host)
        host, port = args.direct_dns.rsplit(':', 1)
        if ipaddress.ip_address(host).version != 4 or not 1 <= int(port) <= 65535:
            raise ValueError()
    except ValueError:
        parser.error('target IP or direct IPv4 DNS endpoint invalid')
    return args


async def main(args, secret):
    owner = asyncio.current_task()
    asyncio.get_running_loop().add_signal_handler(signal.SIGTERM, owner.cancel)
    success = True
    for index in range(args.samples):
        metadata = dict(version=1, variant=args.variant, server=args.server, campaign=args.campaign,
                        phase=args.phase, pair=args.pair, position=args.position if args.samples == 1 else index,
                        arm=args.arm, sample=args.sample + index, carrier=args.carrier, backend=args.backend,
                        half_close=args.half_close)
        output = Results(args.results, metadata)
        runner = Runner(args, secret, output)
        start = time.monotonic()
        try:
            executable = Path(args.binary or BINARIES[args.variant])
            output.emit(dict(kind='manifest', client_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                             cert_sha256=hashlib.sha256(Path(args.cert).read_bytes()).hexdigest() if args.cert else None,
                             script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                             adapter_sha256=hashlib.sha256(Path(__file__).with_name('adapter.py').read_bytes()).hexdigest(),
                             domain_sha256=hashlib.sha256(args.domain.encode()).hexdigest(),
                             target_sha256=hashlib.sha256(f'{args.target_host}:{args.target_port}'.encode()).hexdigest(),
                             direct_dns_sha256=hashlib.sha256(args.direct_dns.encode()).hexdigest(),
                             fixture_cert_sha256=hashlib.sha256(Path(args.fixture_cert).read_bytes()).hexdigest()
                             if args.fixture_cert else None,
                             python=platform.python_version(), machine=platform.machine(), cc='dcubic',
                             workers=0 if args.carrier == 'direct' else 56, queue=0 if args.carrier == 'direct' else 64,
                             paths=2 if args.carrier == 'multipath' else 1,
                             deadline=args.deadline, startup_deadline=args.startup_deadline,
                             payload='synthetic-x', sizes=SIZES, download=1048576,
                             baseline_flow_open=args.baseline_flow_open, pinned=args.variant != 'C0',
                             authenticated_socks=args.variant != 'C0'))
            await runner.start()
            success = await runner.workload() and success
        except (OSError, ValueError, KeyError, EOFError, TimeoutError) as error:
            output.emit(dict(kind='startup', ok=False, error=type(error).__name__,
                             seconds=time.monotonic() - start, acknowledged=0, ack_Bps=0,
                             timeout=isinstance(error, TimeoutError)))
            # Preserve each offered workload as zero goodput on startup failure.
            for opcode, size, condition in [('U', n, 'sequential') for n in SIZES] + [
                ('D', 1048576, 'sequential'), ('D', 1048576, 'two-flows'), ('D', 1048576, 'two-flows')] + [
                ('U', n, 'loaded') for n in SIZES] + [('U', 8192, 'recovery')]:
                output.emit(dict(kind='request', opcode=opcode, requested=size, condition=condition,
                                 ok=False, error='StartupFailure', acknowledged=0, ack_Bps=0,
                                 timeout=isinstance(error, TimeoutError), seconds=time.monotonic() - start))
            output.emit(dict(kind='window', ok=False, failures=12, acknowledged=0, ack_Bps=0))
            success = False
        finally:
            await runner.cleanup()
            output.output.close()
    return success


if __name__ == '__main__':
    args = arguments()
    if args.schedule:
        print(json.dumps([dict(pair=p, position=i, arm=arm) for p, i, arm in schedule(args.phase)]))
        sys.exit(0)
    try:
        secret = protected_json(args.credentials) if args.credentials else {}
        if (args.variant != 'C0' or args.baseline_flow_open) and 'flow_token' not in secret:
            raise ValueError('Flow token required')
        if args.backend == 'https' and 'fixture_token' not in secret:
            raise ValueError('fixture token required')
        sys.exit(0 if asyncio.run(main(args, secret)) else 1)
    except (ValueError, KeyError, OSError):
        print('{"kind":"input","ok":false}', file=sys.stderr)
        sys.exit(2)
    except (KeyboardInterrupt, asyncio.CancelledError):
        sys.exit(130)
