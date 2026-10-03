"""Synthetic client/server feedback reachability; only aggregates leave the fixture.

Usage: python3 tests/probe_server_feedback.py BUILD RUST_CLIENT [--delay-ms 100]
Build test-server-feedback explicitly first. No production endpoints or secrets.
"""
import argparse
import hashlib
import heapq
import json
import os
import pathlib
import random
import selectors
import socket
import subprocess
import sys
import tempfile
import threading
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=pathlib.Path)
parser.add_argument("rust_client", type=pathlib.Path)
parser.add_argument("--delay-ms", type=float, default=0)
parser.add_argument("--capacity", type=int, default=4096)
parser.add_argument("--c-client", action="store_true")
parser.add_argument("--loss-percent", type=float, default=0)
parser.add_argument("--max-operations", type=int, default=200000)
args = parser.parse_args()
args.build = args.build.resolve()
args.rust_client = args.rust_client.resolve()
assert 0 <= args.delay_ms <= 500
assert 1 <= args.capacity <= 65536
assert 0 <= args.loss_percent <= 2
assert 1 <= args.max_operations <= 1000000
# Reuse the tested synthetic Flow backend and SOCKS framing, not bench code.
sys.argv = [sys.argv[0], str(args.build)]
from test_runtime import Backend, Parity, exact, port


class Carrier:
    """Per-peer connected upstream; delayed datagrams keep their original peer.

    Two half-delays model RTT; pending work is bounded and drops are counted.
    This is a synthetic UDP carrier, not a recursive resolver.
    """
    def __init__(self, target, delay_ms):
        self.target = ("127.0.0.1", target)
        self.delay = delay_ms / 2000
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.port = self.socket.getsockname()[1]
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.socket, selectors.EVENT_READ, None)
        self.peers, self.pending = {}, []
        self.serial = self.queries = self.replies = self.drops = self.peak = 0
        self.losses = 0
        self.rng = random.Random(15)
        self.closed = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def queue(self, sock, data, peer=None):
        if self.queries + self.replies > args.max_operations:
            self.drops += 1
            return
        if self.rng.random() < args.loss_percent / 100:
            self.losses += 1
            return
        if len(self.pending) >= args.capacity:
            self.drops += 1
            return
        self.serial += 1
        heapq.heappush(self.pending, (time.monotonic() + self.delay, self.serial, sock, data, peer))
        self.peak = max(self.peak, len(self.pending))

    def run(self):
        while not self.closed.is_set():
            now = time.monotonic()
            while self.pending and self.pending[0][0] <= now:
                _, _, sock, data, peer = heapq.heappop(self.pending)
                if peer is None:
                    sock.send(data)
                else:
                    sock.sendto(data, peer)
            wait = max(0, min(.01, self.pending[0][0] - now)) if self.pending else .01
            for key, _ in self.selector.select(wait):
                data, peer = key.fileobj.recvfrom(4096)
                if key.data is None:
                    self.queries += 1
                    if peer not in self.peers:
                        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        upstream.connect(self.target)
                        self.peers[peer] = upstream
                        self.selector.register(upstream, selectors.EVENT_READ, peer)
                    self.queue(self.peers[peer], data)
                else:
                    self.replies += 1
                    self.queue(self.socket, data, key.data)

    def close(self):
        self.closed.set()
        self.thread.join(2)
        assert not self.thread.is_alive()
        self.selector.close()
        self.socket.close()
        for sock in self.peers.values():
            sock.close()


def stop(child):
    if child.poll() is None:
        child.terminate()
    try:
        child.wait(3)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()


def run(loaded, cert, key):
    helper = Parity()
    helper.credentials = os.urandom(16) + os.urandom(16).hex().encode() + os.urandom(16).hex().encode()
    backend = Backend(helper.credentials, "probe")
    dns, local = port(socket.SOCK_DGRAM), port()
    server = subprocess.Popen([str(args.build / "test-server-feedback"),
        "--dns-listen-host", "127.0.0.1", "--dns-listen-port", str(dns),
        "--target-address", f"127.0.0.1:{backend.port}", "--cert", str(cert),
        "--key", str(key), "--domain", "t.test"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    carrier = Carrier(dns, args.delay_ms)
    client = None
    ended = threading.Event()
    download_ready = threading.Event()
    download = {"bytes": 0, "errors": 0}
    measurements = []
    thread = None
    try:
        time.sleep(.15)
        assert server.poll() is None
        env = {k: v for k, v in os.environ.items()
               if k not in ("SSLKEYLOGFILE", "QLOGDIR", "QLOG_DIR", "RUST_LOG")}
        client_args = [str(args.rust_client), "--tcp-listen-host", "127.0.0.1",
            "--tcp-listen-port", str(local), "--domain", "t.test", "--cert", str(cert),
            "--flow-relay-stdin"]
        client_args += (["--resolver", f"127.0.0.1:{carrier.port}"] if args.c_client else
            ["--congestion-control", "dcubic", "--authoritative", f"127.0.0.1:{carrier.port}"])
        client = subprocess.Popen(client_args, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE if args.c_client else subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, env=env)
        client.stdin.write(helper.credentials)
        client.stdin.close()
        client.stdin = None

        def load():
            try:
                while not ended.is_set():
                    with helper.socks(local) as sock:
                        sock.settimeout(30)
                        sock.sendall(b"D" + (4 * 1024 * 1024).to_bytes(4, "big"))
                        left = 4 * 1024 * 1024
                        while left and not ended.is_set():
                            data = sock.recv(min(16384, left))
                            assert data and data == b"x" * len(data)
                            download["bytes"] += len(data)
                            left -= len(data)
                            download_ready.set()
            except Exception:
                if not ended.is_set():
                    download["errors"] += 1
                download_ready.set()

        if loaded:
            thread = threading.Thread(target=load, daemon=True)
            thread.start()
            assert download_ready.wait(15) and download["bytes"] > 0
        for size in (524288, 4194304):
            start = time.monotonic()
            with helper.socks(local) as sock:
                sock.settimeout(45)
                sock.sendall(b"U" + size.to_bytes(4, "big") + b"x" * size)
                assert int.from_bytes(exact(sock, 4), "big") == size
            measurements.append({"upload_bytes": size, "acknowledged_seconds": round(time.monotonic() - start, 4)})
        assert server.poll() is None and client.poll() is None
    finally:
        ended.set()
        if client is not None:
            stop(client)
            client_output = client.communicate()[0] if args.c_client else None
        if thread is not None:
            thread.join(3)
        carrier.close()
        stop(server)
        output, errors = server.communicate()
        backend.close()
    assert server.returncode == 0 and not errors
    diagnostics = json.loads(output)
    assert diagnostics["pacing_checks"] > 0 and diagnostics["ack_slots"] > 0
    assert diagnostics["credit_advances"] > 0
    print(json.dumps({"loaded": loaded, "delay_ms": args.delay_ms, "loss_percent": args.loss_percent,
        "capacity": args.capacity, "max_operations": args.max_operations, "uploads": measurements,
        "download": download, "carrier": {"queries": carrier.queries, "replies": carrier.replies,
        "drops": carrier.drops, "losses": carrier.losses, "peak": carrier.peak, "peers": len(carrier.peers)},
        "client": json.loads(client_output) if args.c_client else None,
        "native": diagnostics}, sort_keys=True), flush=True)
    assert not download["errors"] and not carrier.drops, "synthetic carrier/download fixture failed"


if __name__ == "__main__":
    print(json.dumps({"client_sha256": hashlib.sha256(args.rust_client.read_bytes()).hexdigest(),
        "diagnostic_server_sha256": hashlib.sha256((args.build / "test-server-feedback").read_bytes()).hexdigest()}), flush=True)
    with tempfile.TemporaryDirectory() as tmp:
        cert, key = pathlib.Path(tmp) / "cert.pem", pathlib.Path(tmp) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=test.example.com", "-keyout", str(key), "-out", str(cert)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for loaded in (False, True):
            run(loaded, cert, key)
