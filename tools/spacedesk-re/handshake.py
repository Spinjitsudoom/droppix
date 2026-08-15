#!/usr/bin/env python3
"""Capture the spacedesk viewer's TCP handshake.

Discovery is solved: answering the UDP broadcast with `SPACEDESK-NET-SERVER` (no NUL)
makes the viewer open TCP :28252 and send an opening message. This records that
conversation byte-for-byte and tries to decode its structure.
"""
import socket
import struct
import sys
import threading
import time

PORT = 28252
MAGIC = b"SPACEDESK-NET-CLIENT\x00"
REPLY = b"SPACEDESK-NET-SERVER"
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 40
start = time.time()


def log(m):
    print(f"[{time.time()-start:6.2f}s] {m}", flush=True)


def hexdump(data, indent="    "):
    out = []
    for off in range(0, len(data), 16):
        c = data[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    return "\n".join(out)


def analyze(d):
    log(f"  --- structural analysis of {len(d)} bytes ---")
    # Leading integers, both endians: protocol headers usually start with type/length/version.
    for off in (0, 4, 8, 12, 16):
        if off + 4 <= len(d):
            le = struct.unpack_from("<I", d, off)[0]
            be = struct.unpack_from(">I", d, off)[0]
            h16 = struct.unpack_from("<H", d, off)[0]
            log(f"  @{off:>2}: u32le={le:<12} u32be={be:<12} u16le={h16}")
    # Any embedded text (spacedesk is a Windows product: UTF-16LE is likely).
    runs = []
    cur = b""
    for b in d:
        if 32 <= b < 127:
            cur += bytes([b])
        else:
            if len(cur) >= 3:
                runs.append(cur.decode())
            cur = b""
    if len(cur) >= 3:
        runs.append(cur.decode())
    if runs:
        log(f"  ascii runs: {runs[:12]}")
    try:
        u = d.decode("utf-16-le", errors="ignore")
        pr = "".join(c if c.isprintable() else "·" for c in u)
        keep = "".join(c for c in pr if c != "·")
        if len(keep) >= 4:
            log(f"  utf16le text: {keep[:120]!r}")
    except Exception:
        pass
    # Plausible screen dimensions anywhere in the message.
    for off in range(0, len(d) - 3, 2):
        a, b2 = struct.unpack_from("<HH", d, off)
        if a in (640, 720, 800, 1024, 1080, 1280, 1440, 1600, 1920, 2160, 2340, 2400) and \
           b2 in (480, 600, 720, 768, 900, 1024, 1080, 1200, 1440, 1920, 2160, 1280):
            log(f"  possible WxH at @{off}: {a}x{b2}")


def udp_loop(stop):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("0.0.0.0", PORT))
    s.settimeout(0.5)
    n = 0
    while not stop.is_set():
        try:
            data, addr = s.recvfrom(65535)
        except socket.timeout:
            continue
        if data.startswith(b"SPACEDESK"):
            # The viewer broadcasts FROM an ephemeral port but listens for server
            # announcements ON :28252, so a reply to the source port alone is ignored.
            # Cover all three paths: source port, the client's :28252, and broadcast.
            for dst in ((addr[0], addr[1]), (addr[0], PORT), ("255.255.255.255", PORT)):
                try:
                    s.sendto(REPLY, dst)
                except OSError:
                    pass
            n += 1
    s.close()


def tcp_loop(stop):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PORT))
    s.listen(8)
    s.settimeout(0.5)
    first = True
    while not stop.is_set():
        try:
            c, addr = s.accept()
        except socket.timeout:
            continue
        if first:
            first = False
            threading.Thread(target=session, args=(c, addr, stop), daemon=True).start()
        else:
            c.close()   # one at a time; the app retries aggressively
    s.close()


def session(c, addr, stop):
    log(f"=== TCP session from {addr[0]}:{addr[1]} ===")
    c.settimeout(12)
    msgno = 0
    try:
        while not stop.is_set():
            d = c.recv(65535)
            if not d:
                log("  client closed")
                break
            msgno += 1
            log(f"  [msg {msgno}] {len(d)} bytes")
            print(hexdump(d), flush=True)
            if msgno <= 3:
                analyze(d)
    except socket.timeout:
        log("  (idle 12s — client may be waiting for the SERVER to speak first)")
    except OSError as e:
        log(f"  error: {e}")
    finally:
        c.close()


stop = threading.Event()
threading.Thread(target=udp_loop, args=(stop,), daemon=True).start()
threading.Thread(target=tcp_loop, args=(stop,), daemon=True).start()
log(f"answering discovery with {REPLY!r}; capturing TCP for {DUR}s")
time.sleep(DUR)
stop.set()
time.sleep(0.8)
log("done")
