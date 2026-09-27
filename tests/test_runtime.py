"""Synthetic-only loopback parity gates; no production addresses or retained native output."""
import concurrent.futures
import contextlib
import os
import pathlib
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

BUILD = pathlib.Path(sys.argv.pop(1)).resolve()

def port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

def exact(s, n):
    b = bytearray()
    while len(b) < n:
        r = s.recv(n - len(b))
        if not r:
            raise EOFError("premature EOF")
        b.extend(r)
    return bytes(b)

class Backend:
    def __init__(self, credentials, mode="echo"):
        self.credentials, self.mode = credentials, mode
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0)); self.listener.listen(64)
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.opens = []
        self.errors = []
        self.closed = False
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.closed:
            try:
                s, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self.handle, args=(s,), daemon=True).start()

    def handle(self, s):
        try:
            with s:
                s.settimeout(15)
                tag = exact(s, 1)[0]
                token = exact(s, 16)
                if token != self.credentials[:16]:
                    return
                atype = (tag >> 1) & 3
                address = exact(s, 4 if atype == 0 else 16) if atype < 2 else exact(s, exact(s, 1)[0])
                destination_port = exact(s, 2)
                self.opens.append((tag, address, destination_port))
                if self.mode == "probe":
                    opcode = exact(s, 1)
                    size = int.from_bytes(exact(s, 4), "big")
                    if opcode == b"D":
                        s.sendall(b"x" * size)
                    elif opcode == b"U":
                        payload = exact(s, size)
                        if payload != b"x" * size:
                            raise ValueError("synthetic upload mismatch")
                        s.sendall(size.to_bytes(4, "big"))
                    else:
                        raise ValueError("invalid synthetic probe opcode")
                    while s.recv(1024):
                        pass
                    return
                if self.mode == "download":
                    s.sendall(b"x" * self.download_size)
                    return
                if self.mode == "remote-fin":
                    s.sendall(b"reply-before-upload")
                    s.shutdown(socket.SHUT_WR)
                all_data = bytearray()
                while True:
                    data = s.recv(16384)
                    if not data:
                        break
                    if self.mode == "echo":
                        s.sendall(data)
                    else:
                        all_data.extend(data)
                if self.mode == "after-fin":
                    s.sendall(all_data)
                elif self.mode == "remote-fin":
                    self.upload = bytes(all_data)
        except Exception as e:
            self.errors.append(type(e).__name__)

    def close(self):
        self.closed = True; self.listener.close(); self.thread.join(1)

class Relay:
    """Independent UDP endpoints preserving response origin, with switchable loss."""
    def __init__(self, target):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0)); self.sock.settimeout(.05)
        self.port = self.sock.getsockname()[1]
        self.target = ("127.0.0.1", target)
        self.upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.upstream.settimeout(.05)
        self.client = None
        self.count = 0
        self.drop = False
        self.closed = False
        self.thread = threading.Thread(target=self.run, daemon=True); self.thread.start()

    def run(self):
        while not self.closed:
            try:
                ready, _, _ = select.select([self.sock, self.upstream], [], [], .05)
            except (OSError, ValueError):
                break
            try:
                if self.sock in ready:
                    data, self.client = self.sock.recvfrom(4096)
                    self.count += 1
                    if not self.drop:
                        self.upstream.sendto(data, self.target)
            except socket.timeout:
                pass
            except OSError:
                break
            try:
                if self.upstream in ready:
                    data, _ = self.upstream.recvfrom(4096)
                    if not self.drop and self.client:
                        self.sock.sendto(data, self.client)
            except socket.timeout:
                pass
            except OSError:
                break

    def close(self):
        self.closed = True; self.sock.close(); self.upstream.close(); self.thread.join(1)

class Parity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.cert = pathlib.Path(cls.tmp.name) / "cert.pem"
        cls.key = pathlib.Path(cls.tmp.name) / "key.pem"
        cls.wrong = pathlib.Path(cls.tmp.name) / "wrong.pem"
        for cert in (cls.cert, cls.wrong):
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                            "-subj", "/CN=test.example.com", "-keyout", str(cls.key), "-out", str(cert)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Generate matching production-test key/cert last.
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=test.example.com", "-keyout", str(cls.key), "-out", str(cls.cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.credentials = os.urandom(16) + os.urandom(16).hex().encode() + os.urandom(16).hex().encode()
        self.children = []
        self.resources = []

    def tearDown(self):
        for p in reversed(self.children):
            if p.poll() is None:
                p.terminate()
            try:
                p.wait(3)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait()
            output, errors = p.communicate()
            self.assertEqual(output, b"", "native stdout retained metadata")
            if b"Sanitizer" in errors:
                sys.stderr.write(errors.decode(errors="replace"))
            self.assertEqual(errors, b"", "native stderr retained metadata")
            self.assertGreaterEqual(p.returncode, 0, pathlib.Path(p.args[0]).name + " crashed")
        for r in reversed(self.resources):
            r.close()

    def spawn(self, name, args, credentials=None):
        p = subprocess.Popen([str(BUILD / name), *map(str, args)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if credentials is not None:
            p.stdin.write(credentials)
        p.stdin.close(); p.stdin = None
        self.children.append(p)
        return p

    def server(self, mode="echo", extra=()):
        b = Backend(self.credentials, mode); self.resources.append(b)
        dns = port(socket.SOCK_DGRAM)
        p = self.spawn("slipstream-server", ["--dns-listen-port", dns, "--target-address", f"127.0.0.1:{b.port}",
            "--cert", self.cert, "--key", self.key, "--domain", "t.test", "--domain", "tt.test", *extra])
        time.sleep(.15)
        self.assertIsNone(p.poll())
        return b, dns, p

    def client(self, resolvers, domain="t.test", pin=None, credentials=None):
        local = port()
        args = ["--tcp-listen-port", local, "--domain", domain, "--cert", pin or self.cert, "--flow-relay-stdin"]
        for resolver in resolvers:
            args += ["--resolver", f"127.0.0.1:{resolver}"]
        p = self.spawn("slipstream-client", args, self.credentials if credentials is None else credentials)
        return p, local

    def socks(self, local, address=b"example.com", uot=False, password=None):
        deadline = time.monotonic() + 9
        while True:
            try:
                s = socket.create_connection(("127.0.0.1", local), timeout=.3); break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(.03)
        s.settimeout(10)
        s.sendall(b"\x05\x01\x02"); self.assertEqual(exact(s, 2), b"\x05\x02")
        s.sendall(b"\x01\x20" + self.credentials[16:48] + b"\x20" + (password or self.credentials[48:]))
        reply = exact(s, 2)
        if password:
            self.assertEqual(reply, b"\x01\x01"); return s
        self.assertEqual(reply, b"\x01\x00")
        name = b"sp.v2.udp-over-tcp.arpa" if uot else address
        s.sendall(b"\x05\x01\x00\x03" + bytes([len(name)]) + name + (b"\x00\x00" if uot else b"\x01\xbb"))
        self.assertEqual(exact(s, 10), b"\x05\x00\x00\x01" + b"\x00"*6)
        if uot:
            s.sendall(b"\x00\x03" + bytes([len(address)]) + address + b"\x00\x35")
        return s

    def test_payload_suffixes_and_parallel_streams(self):
        b, dns, server = self.server()
        for suffix in ("t.test", "tt.test"):
            child, local = self.client([dns], suffix)
            def transfer(i):
                data = os.urandom(32768 + i)
                with self.socks(local) as s:
                    s.sendall(data); s.shutdown(socket.SHUT_WR)
                    self.assertEqual(exact(s, len(data)), data)
                    self.assertEqual(s.recv(1), b"")
            with concurrent.futures.ThreadPoolExecutor(2) as pool:
                list(pool.map(transfer, range(2)))
            child.terminate(); child.wait(3)
        self.assertEqual(len(b.opens), 4)
        self.assertFalse(b.errors)
        self.assertIsNone(server.poll())

    def test_halfclose_local_fin(self):
        b, dns, _ = self.server("after-fin")
        _, local = self.client([dns])
        data = os.urandom(512 * 1024)
        with self.socks(local) as s:
            s.sendall(data); s.shutdown(socket.SHUT_WR)
            self.assertEqual(exact(s, len(data)), data)
            self.assertEqual(s.recv(1), b"")
        self.assertFalse(b.errors)

    def test_halfclose_remote_fin(self):
        b, dns, _ = self.server("remote-fin")
        _, local = self.client([dns])
        data = os.urandom(32768)
        with self.socks(local) as s:
            time.sleep(.2)
            self.assertEqual(len(b.opens), 1, "OPEN was not delivered without application payload")
            self.assertEqual(exact(s, 19), b"reply-before-upload")
            self.assertEqual(s.recv(1), b"")
            s.sendall(data); s.shutdown(socket.SHUT_WR)
        deadline = time.monotonic() + 5
        while not hasattr(b, "upload") and time.monotonic() < deadline:
            time.sleep(.05)
        self.assertEqual(getattr(b, "upload", None), data)

    def test_uot_records_and_auth(self):
        b, dns, _ = self.server()
        _, local = self.client([dns])
        with self.socks(local, password=b"0"*32) as s:
            self.assertEqual(s.recv(1), b"")
        self.assertEqual(len(b.opens), 0)
        record = b"\x02\x0bexample.com\x00\x35\x00\x05nonce"
        with self.socks(local, uot=True) as s:
            s.sendall(record); self.assertEqual(exact(s, len(record)), record)
        self.assertEqual(b.opens[0][0], 0x2c)

    def test_bootstrap_dead_primary(self):
        _, dns, _ = self.server()
        dead = Relay(dns); dead.drop = True; self.resources.append(dead)
        child, local = self.client([dead.port, dns])
        with self.socks(local) as s:
            s.sendall(b"bootstrap"); self.assertEqual(exact(s, 9), b"bootstrap")
        self.assertIsNone(child.poll())

    def test_bootstrap_both_dead(self):
        child, _ = self.client([port(socket.SOCK_DGRAM), port(socket.SOCK_DGRAM)])
        self.assertNotEqual(child.wait(8), 0)

    def test_multipath_loss_and_idle_recovery(self):
        _, dns, server = self.server()
        paths = [Relay(dns), Relay(dns)]; self.resources.extend(paths)
        child, local = self.client([p.port for p in paths])
        with self.socks(local) as s:
            s.sendall(b"start"); self.assertEqual(exact(s, 5), b"start")
            time.sleep(1.5)
            self.assertTrue(all(p.count for p in paths), "both endpoints must validate")
            before = sum(p.count for p in paths)
            time.sleep(2.2)
            self.assertLessEqual(sum(p.count for p in paths) - before, 6, "quiet polling must be bounded")
            paths[0].drop = True
            data = os.urandom(32768)
            s.sendall(data)
            self.assertEqual(exact(s, len(data)), data)
            s.shutdown(socket.SHUT_WR)
            self.assertEqual(s.recv(1), b"")
        time.sleep(1.5)
        before = sum(p.count for p in paths)
        time.sleep(2.2)
        self.assertLessEqual(sum(p.count for p in paths) - before, 3, "empty streams must not explicitly poll")
        self.assertIsNone(child.poll()); self.assertIsNone(server.poll())

    def test_stalled_setup_is_bounded(self):
        b, dns, _ = self.server()
        _, local = self.client([dns])
        with self.socks(local) as ready:
            ready.sendall(b"ready"); self.assertEqual(exact(ready, 5), b"ready")
        with socket.create_connection(("127.0.0.1", local), timeout=1) as s:
            s.settimeout(7)
            s.sendall(b"\x05")
            self.assertEqual(s.recv(1), b"")
        self.assertEqual(len(b.opens), 1)

    def test_wrong_pin_and_wrong_suffix(self):
        b, dns, _ = self.server()
        for kwargs in ({"pin": self.wrong}, {"domain": "untrusted.test"}):
            child, _ = self.client([dns], **kwargs)
            self.assertNotEqual(child.wait(5), 0)
        self.assertEqual(len(b.opens), 0)

    def test_invalid_credentials(self):
        for value in (self.credentials[:-1], self.credentials+b"x", self.credentials[:16]+b"A"*64):
            child, _ = self.client([port(socket.SOCK_DGRAM)], credentials=value)
            self.assertNotEqual(child.wait(2), 0)

    def test_wrong_flow_token(self):
        b, dns, _ = self.server()
        _, local = self.client([dns], credentials=os.urandom(16) + self.credentials[16:])
        with self.socks(local) as s:
            s.sendall(b"must-not-reach-backend")
            self.assertEqual(s.recv(1), b"")
        self.assertEqual(len(b.opens), 0)

    def test_connection_cleanup_and_malformed_dns(self):
        b, dns, server = self.server(extra=("--max-connections", 1, "--idle-timeout-seconds", 1))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            for length in (1, 2, 11, 12, 63, 255):
                probe.sendto(os.urandom(length), ("127.0.0.1", dns))
        for _ in range(3):
            child, local = self.client([dns])
            with self.socks(local) as s:
                s.sendall(b"cleanup"); self.assertEqual(exact(s, 7), b"cleanup")
            time.sleep(1.5)
            child.terminate(); self.assertIn(child.wait(3), (0, 1))
            self.assertIsNone(server.poll())
        self.assertEqual(len(b.opens), 3)

if __name__ == "__main__":
    unittest.main()
