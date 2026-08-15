#!/usr/bin/env python3
"""Hunt for the spacedesk server reply, many candidates per tap.

The viewer stays connected and polls with type-12 keepalives while it waits for a server,
so a single tap buys a whole batch of experiments instead of one. Progress is persisted,
so a disconnect resumes at the next untried candidate rather than starting over.

Signal we are looking for, in order of strength:
  1. the client emits a message type we have never seen from it (currently: 0, 12)
  2. the client stops keepaliving (it may be waiting on us for the next step)
  3. the client hangs up immediately (candidate actively rejected — also informative)
"""
import json
import os
import socket
import struct
import sys
import threading
import time

PORT = 28252
UDP_REPLY = b"SPACEDESK-NET-SERVER"
HDR = 128
HERE = os.path.dirname(os.path.abspath(__file__))
STATE = os.path.join(HERE, "hunt_state.json")
LOG = os.path.join(HERE, "hunt.log")
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 240
PER = 2.5           # observation window per candidate
KNOWN_CLIENT_TYPES = {0, 12}
start = time.time()


def log(m):
    line = f"[{time.time()-start:6.2f}s] {m}"
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")


def hexdump(d, indent="      "):
    out = []
    for off in range(0, min(len(d), 320), 16):
        c = d[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    return "\n".join(out)


def hdr(mtype, payload=b"", **f):
    h = bytearray(HDR)
    struct.pack_into("<I", h, 0, mtype)
    struct.pack_into("<I", h, 4, len(payload))
    for k, v in f.items():
        struct.pack_into("<I", h, int(k[1:]), v)
    return bytes(h) + payload


# Server display geometry written where the client wrote its own.
GEO = {"o52": 1920, "o88": 1080}
# Fields the client set in its hello, minus the client-specific ones — a plausible shape
# for a symmetric server hello.
SHAPE = {"o8": 4, "o12": 8, "o20": 3, "o24": 3, "o28": 2, "o32": 60, "o48": 1, "o124": 1}


def build_candidates():
    c = []
    for t in range(0, 16):
        c.append((f"type={t} geom", hdr(t, b"", **GEO)))
    for t in (0, 1, 2, 3):
        c.append((f"type={t} geom+shape", hdr(t, b"", **GEO, **SHAPE)))
    c.append(("type=0 bare", hdr(0)))
    c.append(("type=1 bare", hdr(1)))
    # Some protocols ack by echoing the peer's frame back verbatim.
    c.append(("echo client hello", None))          # filled in at runtime
    # A server hello carrying a UTF-16LE name payload, mirroring the client's GUID+model.
    name = "{00000000-0000-0000-0000-000000000001}\x00droppix".encode("utf-16-le")
    for t in (0, 1, 2):
        c.append((f"type={t} geom+name", hdr(t, name, **GEO)))
    return c


CANDIDATES = build_candidates()


def load_i():
    try:
        with open(STATE) as f:
            return json.load(f).get("i", 0)
    except Exception:
        return 0


def save_i(i):
    with open(STATE, "w") as f:
        json.dump({"i": i}, f)


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
                    s.sendto(UDP_REPLY, dst)
                except OSError:
                    pass
    s.close()


def drain(c, seconds):
    """Collect everything the client sends for `seconds`; return (bytes, set-of-types)."""
    buf = b""
    types = set()
    end = time.time() + seconds
    c.settimeout(0.4)
    while time.time() < end:
        try:
            d = c.recv(65535)
        except socket.timeout:
            continue
        except OSError:
            return buf, types, True
        if not d:
            return buf, types, True
        buf += d
        for off in range(0, max(0, len(d) - 3), HDR):
            if off + 4 <= len(d):
                types.add(struct.unpack_from("<I", d, off)[0])
    return buf, types, False


def session(c, addr, stop):
    log(f"=== connection from {addr[0]} ===")
    c.settimeout(8)
    try:
        hello = c.recv(65535)
    except OSError:
        return
    if not hello:
        return
    log(f"  client hello {len(hello)}B")

    i = load_i()
    while i < len(CANDIDATES) and not stop.is_set():
        name, payload = CANDIDATES[i]
        if payload is None:                      # "echo client hello"
            payload = hello
        try:
            c.sendall(payload)
        except OSError:
            log(f"  [{i}] {name}: send failed (client gone)")
            save_i(i + 1)
            return
        data, types, closed = drain(c, PER)
        novel = types - KNOWN_CLIENT_TYPES
        if novel:
            log(f"  [{i}] {name}: *** NEW CLIENT TYPES {sorted(novel)} *** ({len(data)}B)")
            print(hexdump(data), flush=True)
            save_i(i + 1)
            return
        if closed:
            log(f"  [{i}] {name}: client DISCONNECTED (rejected) after {len(data)}B")
            save_i(i + 1)
            return
        quiet = "" if data else "  <- client went QUIET (possible progress)"
        log(f"  [{i}] {name}: {len(data)}B types={sorted(types)}{quiet}")
        i += 1
        save_i(i)
    if i >= len(CANDIDATES):
        log("  all candidates exhausted")


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
        try:
            session(c, a, stop)
        finally:
            c.close()
    s.close()


if "--reset" in sys.argv:
    save_i(0)
    log("state reset")
stop = threading.Event()
threading.Thread(target=udp_loop, args=(stop,), daemon=True).start()
threading.Thread(target=tcp_loop, args=(stop,), daemon=True).start()
log(f"{len(CANDIDATES)} candidates, resuming at #{load_i()}. Tap the server in the app.")
time.sleep(DUR)
stop.set()
time.sleep(0.7)
log(f"done — next candidate index: {load_i()}")
