"""Bounded synthetic AB01 fixed TCP target. No forwarding and no output."""
import argparse
import asyncio
import hashlib
import hmac
from pathlib import Path
import signal
import struct

MAX_SIZE = 1048576
CHUNK = b'x' * 16384


async def close_writer(writer):
    writer.close()
    try:
        await asyncio.wait_for(writer.wait_closed(), 1)
    except (OSError, TimeoutError):
        writer.transport.abort()


class Backend:
    def __init__(self, timeout=90, connections=8, token=None):
        self.timeout, self.connections = timeout, connections
        self.token = token
        self.active = set()

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        if len(self.active) >= self.connections:
            await close_writer(writer)
            return
        self.active.add(task)
        try:
            async with asyncio.timeout(self.timeout):
                if self.token is not None:
                    # Fixed synthetic target, never an arbitrary-destination proxy.
                    opening = await reader.readexactly(23)
                    if (opening[0] != 0x20 or
                            not hmac.compare_digest(opening[1:17], self.token) or
                            opening[17:] != b'\x7f\x00\x00\x01' + struct.pack('!H', 40100)):
                        return
                header = await reader.readexactly(9)
                size = struct.unpack('!I', header[5:])[0]
                if header[:4] != b'AB01' or header[4:5] not in (b'D', b'U', b'E'):
                    return
                if not 1 <= size <= MAX_SIZE:
                    return
                digest = hashlib.sha256()
                if header[4:5] == b'D':
                    for offset in range(0, size, len(CHUNK)):
                        chunk = CHUNK[:min(len(CHUNK), size - offset)]
                        digest.update(chunk)
                        writer.write(chunk)
                        await writer.drain()
                    if await reader.readexactly(32) != digest.digest():
                        return
                    writer.write(b'OK' + digest.digest())
                else:
                    remaining = size
                    while remaining:
                        chunk = await reader.readexactly(min(16384, remaining))
                        # This fixture accepts synthetic x bytes only.
                        if chunk != b'x' * len(chunk):
                            return
                        digest.update(chunk)
                        remaining -= len(chunk)
                        if header[4:5] == b'E':
                            writer.write(chunk)
                            await writer.drain()
                    if header[4:5] == b'U':
                        writer.write(b'OK' + digest.digest())
                await writer.drain()
                # One command per connection. Never wait for peer EOF: the two
                # FIN directions are independent, including download digest ACK.
        except (OSError, EOFError, ValueError, TimeoutError):
            pass
        finally:
            await close_writer(writer)
            self.active.discard(task)


async def main(args):
    token = Path(args.token_file).read_bytes() if args.token_file else None
    if token is not None and len(token) != 16:
        raise ValueError('token size')
    fixture = Backend(args.timeout, token=token)
    server = await asyncio.start_server(fixture.handle, '127.0.0.1', args.port,
                                        limit=32768, backlog=8)
    stop = asyncio.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        asyncio.get_running_loop().add_signal_handler(sig, stop.set)
    async with server:
        await stop.wait()
    tasks = list(fixture.active)
    for task in tasks:
        task.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=40100)
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--token-file', help='Optional raw 16-byte Flow OPEN token; fixed target only')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.timeout <= 300:
        parser.error('port/timeout outside bound')
    asyncio.run(main(args))
