#!/usr/bin/env python3
"""Record a GENUINE spacedesk session by sitting between the viewer and a real server.

Guessing the server side one message at a time is hopeless: the viewer only connects when
a human taps it, so every guess costs a tap. Instead, run the real spacedesk DRIVER in a
Windows VM and put this in the middle — the phone connects here, we relay to the real
server, and both directions are logged with the framing decoded. One tap yields the whole
protocol: server replies, format negotiation, and the video framing.

Setup (WinBoat / dockur-windows):
  1. ~/.winboat/docker-compose.yml:  USER_PORTS: "7148,28252"
  2. docker compose -f ~/.winboat/docker-compose.yml up -d
  3. install the spacedesk DRIVER (server) inside Windows
  4. mitm.py --server 127.0.0.1:<host-mapped-28252>

We answer discovery ourselves so the phone connects to THIS host rather than finding the
Windows server directly; everything then flows through the relay.

Framing (decoded previously, see docs/superpowers/specs/2026-08-15-spacedesk-protocol-notes.md):
  128-byte header + optional payload; @0 u32le type, @4 u32le payload length.
"""
import argparse
import socket
import struct
import sys
import threading
import time

PORT = 28252
HDR = 128
UDP_REPLY = b"SPACEDESK-NET-SERVER"
start = time.time()
lock = threading.Lock()


def log(m, path=None):
    line = f"[{time.time()-start:8.3f}s] {m}"
    with lock:
        print(line, flush=True)
        if path:
            with open(path, "a") as f:
                f.write(line + "\n")


def hexdump(d, limit=256, indent="        "):
    out = []
    for off in range(0, min(len(d), limit), 16):
        c = d[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    if len(d) > limit:
        out.append(f"{indent}... {len(d)-limit} more bytes")
    return "\n".join(out)


class Framer:
    """Reassembles the 128-byte-header stream so each message is logged whole."""

    def __init__(self, who, logpath, dump_payload):
        self.who = who
        self.buf = b""
        self.n = 0
        self.logpath = logpath
        self.dump_payload = dump_payload

    def feed(self, data):
        self.buf += data
        while len(self.buf) >= HDR:
            mtype, plen = struct.unpack_from("<II", self.buf, 0)
            if plen > 64 * 1024 * 1024:          # not our framing — dump raw and resync
                log(f"{self.who}: unframed {len(self.buf)}B (plen={plen})", self.logpath)
                print(hexdump(self.buf), flush=True)
                self.buf = b""
                return
            if len(self.buf) < HDR + plen:
                return                            # wait for the rest
            msg = self.buf[: HDR + plen]
            self.buf = self.buf[HDR + plen :]
            self.n += 1
            hdr_fields = struct.unpack_from("<32I", msg, 0)
            nz = {f"@{i*4}": v for i, v in enumerate(hdr_fields) if v and i > 1}
            log(f"{self.who} #{self.n} type={mtype} payload={plen}B hdr={nz}", self.logpath)
            if plen and self.dump_payload:
                print(hexdump(msg[HDR:], 192), flush=True)


def fetch_real_discovery(host, timeout=3):
    """Ask the real server for its discovery response so we can serve the authentic one.

    The real reply is a 308-byte struct (machine name UTF-16LE @0, TCP port @260, then
    capability fields) — not the bare magic string that merely happens to be enough to
    coax a connection. Serving the genuine bytes keeps the viewer on its normal path.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto(b"SPACEDESK-NET-CLIENT\x00", (host, PORT))
        d, _ = s.recvfrom(65535)
        return d
    except OSError:
        return None
    finally:
        s.close()


def udp_responder(stop, real_host, logpath):
    reply = fetch_real_discovery(real_host)
    if reply:
        name = reply[:64].decode("utf-16-le", "ignore").rstrip("\x00")
        log(f"serving the REAL server's discovery response ({len(reply)}B, name={name!r})", logpath)
    else:
        reply = UDP_REPLY
        log("real server did not answer discovery; falling back to the bare magic", logpath)
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
                    s.sendto(reply, dst)
                except OSError:
                    pass
    s.close()


def pump(src, dst, framer, stop, tag, logpath):
    total = 0
    try:
        while not stop.is_set():
            d = src.recv(65535)
            if not d:
                break
            total += len(d)
            framer.feed(d)
            dst.sendall(d)
    except OSError as e:
        log(f"{tag}: {e}", logpath)
    finally:
        log(f"{tag}: stream ended after {total} bytes", logpath)
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True, help="real spacedesk server host:port")
    ap.add_argument("--seconds", type=int, default=300)
    ap.add_argument("--log", default="mitm.log")
    ap.add_argument("--payloads", action="store_true", help="hexdump payload bytes too")
    a = ap.parse_args()
    host, _, p = a.server.partition(":")
    sport = int(p or PORT)

    stop = threading.Event()
    threading.Thread(target=udp_responder, args=(stop, host, a.log), daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(8)
    srv.settimeout(0.5)
    log(f"relay ready: phone -> :{PORT} -> {host}:{sport}. Tap the server in the app.", a.log)

    end = time.time() + a.seconds
    while time.time() < end and not stop.is_set():
        try:
            c, addr = srv.accept()
        except socket.timeout:
            continue
        log(f"=== viewer connected from {addr[0]} ===", a.log)
        try:
            up = socket.create_connection((host, sport), timeout=8)
        except OSError as e:
            log(f"!! cannot reach the real server at {host}:{sport}: {e}", a.log)
            log("   is spacedesk DRIVER installed+running in Windows, and 28252 forwarded?", a.log)
            c.close()
            continue
        cf = Framer("viewer->server", a.log, a.payloads)
        sf = Framer("server->viewer", a.log, a.payloads)
        threading.Thread(target=pump, args=(c, up, cf, stop, "viewer->server", a.log), daemon=True).start()
        threading.Thread(target=pump, args=(up, c, sf, stop, "server->viewer", a.log), daemon=True).start()
    stop.set()
    time.sleep(0.5)
    log("done", a.log)


if __name__ == "__main__":
    main()
