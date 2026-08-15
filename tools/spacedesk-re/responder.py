#!/usr/bin/env python3
"""Find the spacedesk discovery RESPONSE format by trial.

The viewer broadcasts a fixed magic (`SPACEDESK-NET-CLIENT\\0`) to UDP :28252 and lists
any server that answers. We don't know the answer format, so cycle through candidates and
watch TCP :28252 — the viewer opening a TCP connection is proof it accepted a reply and
believes a server exists. That connection's opening bytes are the next thing we need
anyway, so we log them in full.

Usage: responder.py [seconds-per-candidate]
"""
import socket
import struct
import sys
import threading
import time

PORT = 28252
MAGIC = b"SPACEDESK-NET-CLIENT\x00"
PER = int(sys.argv[1]) if len(sys.argv) > 1 else 6
HOST = socket.gethostname()[:31] or "droppix"
IP = "192.168.0.101"

def u16le(v): return struct.pack("<H", v)
def u32le(v): return struct.pack("<I", v)

# Ordered guesses, cheapest/most-likely first. The protocol is clearly ASCII-magic based,
# so the server's counterpart magic is the obvious opener; the variants add the kind of
# payload a viewer needs to render a server entry (name, address, dimensions).
CANDIDATES = [
    ("server magic + NUL",            b"SPACEDESK-NET-SERVER\x00"),
    ("server magic, no NUL",          b"SPACEDESK-NET-SERVER"),
    ("magic + hostname",              b"SPACEDESK-NET-SERVER\x00" + HOST.encode() + b"\x00"),
    ("magic + hostname + ip",         b"SPACEDESK-NET-SERVER\x00" + HOST.encode() + b"\x00" + IP.encode() + b"\x00"),
    ("magic(utf16le)",                "SPACEDESK-NET-SERVER\x00".encode("utf-16-le")),
    ("magic + u32 ver + hostname",    b"SPACEDESK-NET-SERVER\x00" + u32le(1) + HOST.encode() + b"\x00"),
    ("echo client magic",             MAGIC),
    ("magic + name + 1920x1080",      b"SPACEDESK-NET-SERVER\x00" + HOST.encode() + b"\x00" + u16le(1920) + u16le(1080)),
]

state = {"idx": 0, "tcp_hits": 0, "replies": 0}
lock = threading.Lock()
start = time.time()


def log(m):
    print(f"[{time.time()-start:6.2f}s] {m}", flush=True)


def hexdump(data, indent="      "):
    out = []
    for off in range(0, min(len(data), 512), 16):
        c = data[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    return "\n".join(out)


def udp_loop():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("0.0.0.0", PORT))
    s.settimeout(0.5)
    while state["idx"] < len(CANDIDATES):
        try:
            data, addr = s.recvfrom(65535)
        except socket.timeout:
            continue
        if MAGIC not in data and not data.startswith(b"SPACEDESK"):
            continue
        with lock:
            i = state["idx"]
            if i >= len(CANDIDATES):
                break
            name, payload = CANDIDATES[i]
            state["replies"] += 1
        try:
            s.sendto(payload, addr)           # unicast back to the asking client
            s.sendto(payload, ("255.255.255.255", PORT))  # and broadcast, in case it filters
        except OSError:
            pass
    s.close()


def tcp_loop():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PORT))
    s.listen(8)
    s.settimeout(0.5)
    while state["idx"] < len(CANDIDATES):
        try:
            c, addr = s.accept()
        except socket.timeout:
            continue
        with lock:
            state["tcp_hits"] += 1
            cand = CANDIDATES[min(state["idx"], len(CANDIDATES)-1)][0]
        log(f"*** TCP CONNECT from {addr[0]} while sending: {cand!r} ***")
        threading.Thread(target=session, args=(c, addr), daemon=True).start()
    s.close()


def session(c, addr):
    c.settimeout(15)
    total = b""
    try:
        while len(total) < 8192:
            d = c.recv(65535)
            if not d:
                break
            total += d
            log(f"    TCP {addr[0]} sent {len(d)} bytes:")
            print(hexdump(d), flush=True)
    except socket.timeout:
        if not total:
            log(f"    TCP {addr[0]}: connected but sent nothing in 15s (server speaks first?)")
    except OSError:
        pass
    finally:
        c.close()


threading.Thread(target=udp_loop, daemon=True).start()
threading.Thread(target=tcp_loop, daemon=True).start()

log(f"trying {len(CANDIDATES)} discovery-response candidates, {PER}s each")
for i, (name, payload) in enumerate(CANDIDATES):
    with lock:
        state["idx"] = i
        before = state["tcp_hits"]
        state["replies"] = 0
    log(f"--- [{i+1}/{len(CANDIDATES)}] {name}: {payload[:48]!r}")
    time.sleep(PER)
    with lock:
        got = state["tcp_hits"] - before
        reps = state["replies"]
    log(f"    replied {reps}x -> TCP connects: {got}")
with lock:
    state["idx"] = len(CANDIDATES)
time.sleep(0.8)
log(f"done. total TCP connects: {state['tcp_hits']}")
