"""Ephemeral authenticated HTTPS payload sink, never a proxy or traffic logger."""
import argparse
import asyncio
import hashlib
import hmac
import json
import os
from pathlib import Path
import signal
import socket
import ssl

VERSION = 2


class Fixture:
    def __init__(self, token, timeout=45):
        self.token, self.timeout = token, timeout
        self.active = set()
        self.stats = dict(tls_complete=0, authenticated=0, upload_complete=0,
                          reply_drained=0, http_timeout=0, io_error=0, close_abort=0)

    async def handle(self, reader, writer):
        self.stats['tls_complete'] += 1
        task = asyncio.current_task()
        if len(self.active) >= 8:
            writer.close()
            return
        self.active.add(task)
        try:
            async with asyncio.timeout(self.timeout):
                header = await reader.readuntil(b'\r\n\r\n')
                if len(header) > 8192:
                    return
                lines = header.decode('ascii').split('\r\n')
                method, path, version = lines[0].split(' ')
                fields = {}
                for line in lines[1:]:
                    if not line:
                        continue
                    key, value = line.split(': ', 1)
                    key = key.lower()
                    if key in fields:
                        return
                    fields[key] = value
                if not hmac.compare_digest(fields.get('authorization', ''), 'Bearer ' + self.token):
                    return
                self.stats['authenticated'] += 1
                if 'transfer-encoding' in fields or version != 'HTTP/1.1':
                    return
                length = int(fields.get('content-length', '0'))
                if not 0 <= length <= 524288:
                    return
                if method == 'POST' and path == '/up' and length > 0:
                    body = await reader.readexactly(length)
                    self.stats['upload_complete'] += 1
                    reply = json.dumps({'bytes': length, 'sha256': hashlib.sha256(body).hexdigest()}).encode()
                elif method == 'GET' and path.startswith('/down?bytes=') and length == 0:
                    size = int(path.removeprefix('/down?bytes='))
                    if not 1 <= size <= 1048576:
                        return
                    reply = os.urandom(size)
                else:
                    return
                peer = writer.get_extra_info('peername')[0]
                fingerprint = hashlib.sha256(b'flowd-dispatch-20260909' + socket.inet_aton(peer)).hexdigest()[:12]
                writer.write(('HTTP/1.1 200 OK\r\nContent-Length: %d\r\n'
                              'X-Sha256: %s\r\nX-Egress-Direct: %d\r\n'
                              'X-Egress-Fingerprint: %s\r\nX-Fixture-Version: %d\r\nConnection: close\r\n\r\n' %
                              (len(reply), hashlib.sha256(reply).hexdigest(), peer == '195.128.101.186',
                               fingerprint, VERSION)).encode() + reply)
                await writer.drain()
                self.stats['reply_drained'] += 1
        except TimeoutError:
            self.stats['http_timeout'] += 1
        except (OSError, EOFError, ValueError):
            self.stats['io_error'] += 1
        finally:
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), 1)
            except (OSError, TimeoutError):
                self.stats['close_abort'] += 1
                writer.transport.abort()
            self.active.discard(task)


async def main(args):
    token = Path(args.token_file).read_text().strip()
    if len(bytes.fromhex(token)) != 32:
        raise ValueError('token')
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.cert, args.key)
    fixture = Fixture(token)
    server = await asyncio.start_server(fixture.handle, args.host, args.port, ssl=context,
                                        ssl_handshake_timeout=5, limit=8192, backlog=8)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(sig, stop.set)
    async with server:
        await stop.wait()
    tasks = list(fixture.active)
    for task in tasks:
        task.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)
    if args.stats_file:
        # Opt-in synthetic benchmark totals only, emitted once after shutdown.
        fd = os.open(args.stats_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w') as output:
            json.dump(fixture.stats, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('host', 'cert', 'key', 'token-file'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--port', type=int, default=40004)
    parser.add_argument('--stats-file', help='Exclusive-create aggregate totals on shutdown; synthetic tests only')
    asyncio.run(main(parser.parse_args()))
