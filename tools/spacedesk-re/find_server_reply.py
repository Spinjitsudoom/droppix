#!/usr/bin/env python3
"""Find the spacedesk server's reply to the viewer's hello.

Known so far (decoded from capture):
  framing   : 128-byte header + optional payload
  header@0  : u32le message type   (client hello = 0, client keepalive = 12)
  header@4  : u32le payload length
  hello     : carries the viewer's screen size and, as UTF-16LE payload,
              "{device-guid}\\0MODEL"

Unknown: what the server must send back. Enumerate candidates across the viewer's own
reconnect attempts — one candidate per TCP connection — and score each by how the client
reacts. Anything other than "immediately hangs up / keeps sending type 12" is progress.
"""
import socket
import struct
import sys
import threading
import time

PORT = 28252
REPLY_UDP = b"SPACEDESK-NET-SERVER"
HDR = 128
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 120
start = time.time()
lock = threading.Lock()


def log(m):
    print(f"[{time.time()-start:6.2f}s] {m}", flush=True)


def hexdump(d, indent="      "):
    out = []
    for off in range(0, min(len(d), 256), 16):
        c = d[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    return "\n".join(out)


def header(mtype, payload=b"", **fields):
    """Build a 128-byte header + payload in the client's own format."""
    h = bytearray(HDR)
    struct.pack_into("<I", h, 0, mtype)
    struct.pack_into("<I", h, 4, len(payload))
    for off, val in fields.items():
        struct.pack_into("<I", h, int(off[1:]), val)
    return bytes(h) + payload


# The server's display geometry, echoed in the shape the client used for its own.
SRV = {"o52": 1920, "o88": 1080}

CANDIDATES = []
# Most likely: a hello-response carrying the SERVER's display info. Enumerate the type.
for t in range(0, 14):
    CANDIDATES.append((f"type {t}, server geometry", header(t, b"", **SRV)))
# A couple of shape variants on the most plausible types.
CANDIDATES += [
    ("type 1, bare", header(1)),
    ("type 0 echo-ish", header(0, b"", o8=4, o12=8, o20=3, o24=3, o28=2, o32=60, o48=1, **SRV)),
    ("type 13, server geometry", header(13, b"", **SRV)),
    ("type 12 (mirror keepalive)", header(12)),
]

state = {"i": 0, "conns": 0}


def udp_loop(stop):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("0.0.0.0", PORT))
    s.settimeout(0.5)
    while not stop.is_set():
        try:
            d, a = s.recvfrom(65535)
        except socket.timeout:
            continue
        if d.startswith(b"SPACEDESK"):
            for dst in ((a[0], a[1]), (a[0], PORT), ("255.255.255.255", PORT)):
                try:
                    s.sendto(REPLY_UDP, dst)
                except OSError:
                    pass
    s.close()


def tcp_loop(stop):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PORT))
    s.listen(16)
    s.settimeout(0.5)
    while not stop.is_set():
        try:
            c, a = s.accept()
        except socket.timeout:
            continue
        with lock:
            i = state["i"]
            state["i"] += 1
            state["conns"] += 1
        if i >= len(CANDIDATES):
            c.close()
            continue
        threading.Thread(target=session, args=(c, a, i, stop), daemon=True).start()
    s.close()


def session(c, addr, i, stop):
    name, payload = CANDIDATES[i]
    c.settimeout(6)
    got = []
    try:
        first = c.recv(65535)
        if not first:
            return
        got.append(first)
        c.sendall(payload)                       # <-- the candidate under test
        deadline = time.time() + 6
        extra = b""
        while time.time() < deadline and not stop.is_set():
            try:
                d = c.recv(65535)
            except socket.timeout:
                break
            if not d:
                break
            extra += d
            got.append(d)
        # Score: did the client say anything NEW (a type we haven't seen from it)?
        types = set()
        for blob in got[1:]:
            if len(blob) >= 4:
                types.add(struct.unpack_from("<I", blob, 0)[0])
        novel = types - {12}
        verdict = "no reply" if not extra else (
            f"*** NEW client msg types {sorted(novel)} ***" if novel else f"keepalive only (types {sorted(types)})")
        log(f"[{i+1}/{len(CANDIDATES)}] {name:<28} -> client sent {len(extra):>5}B  {verdict}")
        if novel:
            for blob in got[1:]:
                if len(blob) >= 4 and struct.unpack_from("<I", blob, 0)[0] in novel:
                    print(hexdump(blob), flush=True)
                    break
    except (socket.timeout, OSError):
        log(f"[{i+1}/{len(CANDIDATES)}] {name:<28} -> connection error/timeout")
    finally:
        c.close()


stop = threading.Event()
threading.Thread(target=udp_loop, args=(stop,), daemon=True).start()
threading.Thread(target=tcp_loop, args=(stop,), daemon=True).start()
log(f"{len(CANDIDATES)} server-reply candidates; one per client reconnect. {DUR}s")
time.sleep(DUR)
stop.set()
time.sleep(0.7)
log(f"done — {state['conns']} client connections seen, {min(state['i'], len(CANDIDATES))} candidates tried")
