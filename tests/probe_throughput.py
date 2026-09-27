"""Local synthetic throughput evidence, with a bounded delayed DNS response relay."""
import argparse
import heapq
import json
import select
import socket
import sys
import time
import threading
import pathlib
import subprocess
from collections import deque

parser = argparse.ArgumentParser()
parser.add_argument("build")
parser.add_argument("--delay-ms", type=float, default=100)
parser.add_argument("--bytes", type=int, default=1048576)
parser.add_argument("--samples", type=int, default=3)
parser.add_argument("--loaded-upload", type=int, default=0)
parser.add_argument("--workers", type=int, default=0)
parser.add_argument("--queue", type=int, default=64)
parser.add_argument("--baseline-build")
args = parser.parse_args()
if args.baseline_build and not args.loaded_upload:
    parser.error("--baseline-build requires --loaded-upload")
sys.argv = [sys.argv[0], args.build]
from test_runtime import Parity, Relay, Backend, exact, port

class BaselineCase(Parity):
    def spawn(self, name, argv, credentials=None):
        # Immutable C0 emits diagnostic output; discard it rather than retaining
        # it. Candidate processes still use the normal strict zero-output gate.
        child = subprocess.Popen([name, *map(str, argv)], stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append(child)
        return child

    def tearDown(self):
        for child in reversed(self.children):
            if child.poll() is None:
                child.terminate()
            try:
                child.wait(3)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait()
            self.assertGreaterEqual(child.returncode, 0, "baseline process crashed")
        self.children.clear()
        super().tearDown()

    def server(self, mode="echo", extra=()):
        backend = Backend(self.credentials, mode); self.resources.append(backend)
        dns = port(socket.SOCK_DGRAM)
        child = self.spawn(str(pathlib.Path(args.baseline_build) / "slipstream-server"),
            ["--dns-listen-port", dns, "--target-address", f"127.0.0.1:{backend.port}",
             "--cert", self.cert, "--key", self.key, "--domain", "t.test"])
        time.sleep(.15); self.assertIsNone(child.poll())
        return backend, dns, child

    def client(self, resolvers):
        local = port()
        argv = ["--tcp-listen-port", local, "--domain", "t.test"]
        for resolver in resolvers:
            argv += ["--resolver", f"127.0.0.1:{resolver}"]
        child = self.spawn(str(pathlib.Path(args.baseline_build) / "slipstream-client"), argv)
        # Warm an acknowledged synthetic flow before measuring raw C0.
        with self.socks(local) as sock:
            sock.sendall(b"U\x00\x00\x00\x00")
            self.assertEqual(exact(sock, 4), b"\x00" * 4)
        return child, local

    def socks(self, local):
        deadline = time.monotonic() + 9
        while True:
            try:
                sock = socket.create_connection(("127.0.0.1", local), timeout=.3); break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(.03)
        sock.settimeout(30)
        sock.sendall(b"\x24" + self.credentials[:16] + b"\x0bexample.com\x01\xbb")
        return sock

class DelayedRelay(Relay):
    def __init__(self, target):
        self.peak_pending = 0
        self.dropped = 0
        super().__init__(target)

    def run(self):
        pending = []
        queued = deque()
        active = 0
        serial = 0
        while not self.closed:
            now = time.monotonic()
            while pending and pending[0][0] <= now:
                _, _, data, client = heapq.heappop(pending)
                try:
                    self.sock.sendto(data, client)
                    if args.workers:
                        active -= 1
                except OSError:
                    return
            while args.workers and queued and active < args.workers:
                try:
                    self.upstream.sendto(queued.popleft(), self.target)
                except OSError:
                    return
                active += 1
            timeout = min(.05, max(0, pending[0][0] - now)) if pending else .05
            try:
                ready, _, _ = select.select([self.sock, self.upstream], [], [], timeout)
                if self.sock in ready:
                    data, self.client = self.sock.recvfrom(4096)
                    self.count += 1
                    if not args.workers or active < args.workers:
                        self.upstream.sendto(data, self.target)
                        if args.workers:
                            active += 1
                    elif len(queued) < args.queue:
                        queued.append(data)
                    else:
                        self.dropped += 1
                if self.upstream in ready:
                    data, _ = self.upstream.recvfrom(4096)
                    if self.client and len(pending) < 1024:
                        serial += 1
                        heapq.heappush(pending, (time.monotonic() + args.delay_ms / 1000,
                                               serial, data, self.client))
                        self.peak_pending = max(self.peak_pending, len(pending))
            except (OSError, ValueError):
                return

Parity.setUpClass()
try:
    for sample in range(args.samples):
        case = BaselineCase() if args.baseline_build else Parity()
        case.setUp()
        try:
            backend, dns, _ = case.server("probe" if args.loaded_upload else "download")
            backend.download_size = args.bytes
            relay = DelayedRelay(dns); case.resources.append(relay)
            _, local = case.client([relay.port])
            if args.loaded_upload:
                stop = threading.Event()
                background = []
                errors = []
                def download():
                    try:
                        while not stop.is_set():
                            with case.socks(local) as s:
                                s.settimeout(30)
                                s.sendall(b"D" + args.bytes.to_bytes(4, "big"))
                                data = exact(s, args.bytes)
                                case.assertEqual(data, b"x" * args.bytes)
                                background.append(len(data))
                    except Exception as e:
                        errors.append(type(e).__name__)
                worker = threading.Thread(target=download)
                worker.start()
                time.sleep(.2)
                try:
                    with case.socks(local) as s:
                        s.settimeout(30)
                        start = time.monotonic()
                        s.sendall(b"U" + args.loaded_upload.to_bytes(4, "big") + b"x" * args.loaded_upload)
                        case.assertEqual(int.from_bytes(exact(s, 4), "big"), args.loaded_upload)
                        elapsed = time.monotonic() - start
                finally:
                    stop.set(); worker.join(35)
                case.assertFalse(worker.is_alive()); case.assertFalse(errors)
                print(json.dumps({"sample": sample, "delay_ms": args.delay_ms,
                    "uploaded": args.loaded_upload, "seconds": elapsed,
                    "background_bytes": sum(background), "queries": relay.count,
                    "peak_delayed_responses": relay.peak_pending, "dropped": relay.dropped}))
                continue
            with case.socks(local) as sock:
                sock.settimeout(30)
                start = time.monotonic()
                data = exact(sock, args.bytes)
                elapsed = time.monotonic() - start
                case.assertEqual(data, b"x" * args.bytes)
                case.assertEqual(sock.recv(1), b"")
            print(json.dumps({"sample": sample, "delay_ms": args.delay_ms,
                              "bytes": len(data), "seconds": elapsed,
                              "Bps": len(data) / elapsed, "queries": relay.count,
                              "peak_delayed_responses": relay.peak_pending}))
        finally:
            case.tearDown()
finally:
    Parity.tearDownClass()
