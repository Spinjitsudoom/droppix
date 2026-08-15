#!/usr/bin/env python3
"""Drive a real spacedesk server by replaying the viewer's captured hello.

The viewer only connects when a human taps it, which made every experiment cost a tap.
But we captured the viewer's exact 462-byte hello once, and a real spacedesk server is
reachable — so we can act as the client ourselves and learn the entire server side with
no phone in the loop.

Replays the hello, then keeps the conversation alive (the viewer sends type-12 keepalives
while waiting) and records everything the server sends, decoding the 128-byte framing.
The prize is whatever carries pixels: its type, size, and cadence tell us whether a
droppix-side implementation is feasible.

Usage: replay.py --server HOST[:PORT] [--seconds N] [--hello FILE] [--keepalive]
"""
import argparse
import binascii
import os
import re
import socket
import struct
import sys
import time

HDR = 128
PORT = 28252
HERE = os.path.dirname(os.path.abspath(__file__))
start = time.time()


def log(m):
    print(f"[{time.time()-start:8.3f}s] {m}", flush=True)


def hexdump(d, limit=192, indent="      "):
    out = []
    for off in range(0, min(len(d), limit), 16):
        c = d[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    if len(d) > limit:
        out.append(f"{indent}... {len(d)-limit} more bytes")
    return "\n".join(out)


def parse_hello(path):
    """Rebuild the captured hello from the probe log's hexdump lines."""
    out = bytearray()
    want = 0
    for line in open(path):
        m = re.match(r"\s+([0-9a-f]{4})\s+((?:[0-9a-f]{2} ?){1,16})", line)
        if not m:
            continue
        off = int(m.group(1), 16)
        if off == 0 and out:
            break                       # second message begins; we only want the first
        if off != want:
            continue
        chunk = binascii.unhexlify(m.group(2).replace(" ", ""))
        out += chunk
        want = off + len(chunk)
    return bytes(out)


def keepalive(seq):
    h = bytearray(HDR)
    struct.pack_into("<I", h, 0, 12)     # observed viewer keepalive type
    struct.pack_into("<I", h, 4, 0)
    struct.pack_into("<I", h, 8, 1220)   # constant seen in captured keepalives
    struct.pack_into("<I", h, 24, seq)
    return bytes(h)


class Framer:
    def __init__(self):
        self.buf = b""
        self.n = 0
        self.types = {}
        self.biggest = (0, None)

    def feed(self, data, dump_first):
        self.buf += data
        while len(self.buf) >= HDR:
            mtype, plen = struct.unpack_from("<II", self.buf, 0)
            if plen > 128 * 1024 * 1024:
                log(f"  !! implausible payload len {plen} (type={mtype}) — resyncing")
                print(hexdump(self.buf), flush=True)
                self.buf = b""
                return
            if len(self.buf) < HDR + plen:
                return
            msg = self.buf[:HDR + plen]
            self.buf = self.buf[HDR + plen:]
            self.n += 1
            self.types[mtype] = self.types.get(mtype, 0) + 1
            if plen > self.biggest[0]:
                self.biggest = (plen, mtype)
            fields = struct.unpack_from("<32I", msg, 0)
            nz = {f"@{i*4}": v for i, v in enumerate(fields) if v and i > 1}
            log(f"  SERVER msg#{self.n} type={mtype} payload={plen}B {nz}")
            if plen and self.types[mtype] <= dump_first:
                print(hexdump(msg[HDR:]), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--seconds", type=float, default=25)
    ap.add_argument("--hello", default=None)
    ap.add_argument("--keepalive", action="store_true", help="send type-12 while waiting")
    ap.add_argument("--dump-first", type=int, default=2, help="hexdump first N of each type")
    a = ap.parse_args()

    hello_path = a.hello or os.path.join(
        os.path.dirname(HERE), "..",
        "tools/spacedesk-re/captured_hello.bin")
    if a.hello and os.path.exists(a.hello) and a.hello.endswith(".bin"):
        hello = open(a.hello, "rb").read()
    else:
        src = a.hello or os.environ.get("HELLO_LOG", "")
        if not src or not os.path.exists(src):
            log("need --hello <probe log with the hexdump, or .bin>")
            sys.exit(2)
        hello = parse_hello(src)
    log(f"replaying client hello: {len(hello)} bytes "
        f"(type={struct.unpack_from('<I', hello, 0)[0]}, "
        f"payload={struct.unpack_from('<I', hello, 4)[0]})")

    host, _, p = a.server.partition(":")
    sock = socket.create_connection((host, int(p or PORT)), timeout=8)
    sock.sendall(hello)
    log("hello sent; recording server output")

    fr = Framer()
    sock.settimeout(0.5)
    end = time.time() + a.seconds
    total = 0
    seq = 3
    last_ka = 0.0
    while time.time() < end:
        try:
            d = sock.recv(262144)
        except socket.timeout:
            if a.keepalive and time.time() - last_ka > 1.0:
                try:
                    sock.sendall(keepalive(seq))
                    seq += 2
                    last_ka = time.time()
                except OSError:
                    break
            continue
        except OSError as e:
            log(f"recv error: {e}")
            break
        if not d:
            log("server closed the connection")
            break
        total += len(d)
        fr.feed(d, a.dump_first)
    sock.close()

    log("=" * 62)
    log(f"total from server: {total} bytes in {fr.n} framed messages")
    log(f"message types seen: {dict(sorted(fr.types.items()))}")
    log(f"largest payload: {fr.biggest[0]}B (type {fr.biggest[1]})")
    if fr.biggest[0] > 10000:
        log(">>> a large payload almost certainly carries PIXELS — inspect its type above")


if __name__ == "__main__":
    main()
