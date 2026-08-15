#!/usr/bin/env python3
"""Passive probe for the spacedesk VIEWER's wire traffic.

spacedesk's own manual documents the server as using TCP + UDP port 28252, which is
above 1024 — so we can bind both and observe the client's discovery broadcast and its
connection attempt without root or packet capture.

We answer nothing yet: the goal of this pass is to record exactly what the viewer sends,
byte for byte, so the discovery and handshake formats can be decoded.

Usage: probe.py [seconds]
"""
import binascii
import socket
import sys
import threading
import time

PORT = 28252
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 180
LOG = "/tmp/claude-1000/-var-mnt-nas-Projects-Spacedesk-for-linux/a8af3c34-a26e-49ca-92d7-62c8feaa58dc/scratchpad/spacedesk/capture.log"
start = time.time()
lock = threading.Lock()


def emit(msg):
    line = f"[{time.time()-start:7.2f}s] {msg}"
    with lock:
        print(line, flush=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")


def hexdump(data, indent="    "):
    out = []
    for off in range(0, len(data), 16):
        chunk = data[off : off + 16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"{indent}{off:04x}  {hexs:<47}  |{text}|")
    return "\n".join(out)


def describe(data):
    """Cheap structural hints: printable runs, UTF-16LE strings, plausible length prefixes."""
    hints = []
    try:
        s = data.decode("utf-16-le", errors="ignore")
        printable = "".join(c for c in s if c.isprintable())
        if len(printable) >= 4:
            hints.append(f"utf16le~ {printable[:80]!r}")
    except Exception:
        pass
    ascii_runs = []
    cur = b""
    for b in data:
        if 32 <= b < 127:
            cur += bytes([b])
        else:
            if len(cur) >= 4:
                ascii_runs.append(cur.decode())
            cur = b""
    if len(cur) >= 4:
        ascii_runs.append(cur.decode())
    if ascii_runs:
        hints.append(f"ascii~ {ascii_runs[:6]}")
    if len(data) >= 4:
        le = int.from_bytes(data[:4], "little")
        be = int.from_bytes(data[:4], "big")
        if le == len(data) or le == len(data) - 4:
            hints.append(f"first u32 LE ({le}) == length prefix")
        if be == len(data) or be == len(data) - 4:
            hints.append(f"first u32 BE ({be}) == length prefix")
    return hints


def udp_listener():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    except OSError:
        pass
    try:
        s.bind(("0.0.0.0", PORT))
    except OSError as e:
        emit(f"UDP bind :{PORT} FAILED: {e}")
        return
    emit(f"UDP listening on :{PORT}")
    s.settimeout(1.0)
    while time.time() - start < DUR:
        try:
            data, addr = s.recvfrom(65535)
        except socket.timeout:
            continue
        except OSError:
            break
        emit(f"UDP  {addr[0]}:{addr[1]} -> :{PORT}   {len(data)} bytes")
        emit(hexdump(data))
        for h in describe(data):
            emit(f"    hint: {h}")
    s.close()


def tcp_listener():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("0.0.0.0", PORT))
    except OSError as e:
        emit(f"TCP bind :{PORT} FAILED: {e}")
        return
    s.listen(8)
    emit(f"TCP listening on :{PORT}")
    s.settimeout(1.0)
    while time.time() - start < DUR:
        try:
            c, addr = s.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        emit(f"TCP  connection from {addr[0]}:{addr[1]}")
        threading.Thread(target=tcp_session, args=(c, addr), daemon=True).start()
    s.close()


def tcp_session(c, addr):
    """Read whatever the viewer sends. We reply with nothing — this pass is observation
    only; a client that hangs up on silence still tells us its opening bytes."""
    c.settimeout(20.0)
    total = 0
    try:
        while True:
            data = c.recv(65535)
            if not data:
                emit(f"TCP  {addr[0]} closed after {total} bytes")
                break
            total += len(data)
            emit(f"TCP  {addr[0]} sent {len(data)} bytes (total {total})")
            emit(hexdump(data))
            for h in describe(data):
                emit(f"    hint: {h}")
    except socket.timeout:
        emit(f"TCP  {addr[0]} idle 20s (sent {total} bytes total)")
    except OSError as e:
        emit(f"TCP  {addr[0]} error: {e}")
    finally:
        c.close()


emit(f"=== spacedesk probe: observing UDP+TCP :{PORT} for {DUR}s ===")
emit("Open the spacedesk VIEWER app, hit scan/refresh, then try connecting to this PC.")
t1 = threading.Thread(target=udp_listener, daemon=True)
t2 = threading.Thread(target=tcp_listener, daemon=True)
t1.start()
t2.start()
t1.join()
t2.join()
emit("=== probe finished ===")
