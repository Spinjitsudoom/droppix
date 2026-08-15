#!/usr/bin/env python3
"""One capture session that yields everything still missing from the spacedesk protocol.

Video and the session handshake are already decoded, because they can be learned by
watching a server send. Input (touch/mouse/keyboard), audio and orientation cannot: they
only appear when a real viewer DRIVES a real server. This relays a genuine session and
records both directions, calling out anything we have not seen before.

Run it, connect the phone, then exercise every input in turn (the prompts below), and the
resulting log pins down each message type.

  python3 capture_all.py --server 172.18.0.2:28252 [--seconds 300]

Output: capture_all.log (annotated) and capture_all.bin (raw, for offline re-analysis).
"""
import argparse
import json
import os
import socket
import struct
import threading
import time

PORT = 28252
HDR = 128
HERE = os.path.dirname(os.path.abspath(__file__))

# What we already understand; anything else is a discovery worth shouting about.
KNOWN = {
    "viewer->server": {0: "hello", 12: "keepalive"},
    "server->viewer": {1: "heartbeat", 2: "video (JPEG stripe)", 3: "display", 4: "ack"},
}

start = time.time()
lock = threading.Lock()
records = []


def log(m, path):
    line = f"[{time.time()-start:8.3f}s] {m}"
    with lock:
        print(line, flush=True)
        with open(path, "a") as f:
            f.write(line + "\n")


def hexdump(d, limit=160, indent="        "):
    out = []
    for off in range(0, min(len(d), limit), 16):
        c = d[off:off+16]
        h = " ".join(f"{b:02x}" for b in c)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"{indent}{off:04x}  {h:<47}  |{t}|")
    if len(d) > limit:
        out.append(f"{indent}... {len(d)-limit} more bytes")
    return "\n".join(out)


def looks_like(payload):
    """Cheap content sniff so audio/image payloads announce themselves."""
    if payload[:3] == b"\xff\xd8\xff":
        return "JPEG"
    if payload[:4] == b"RIFF":
        return "RIFF/WAV audio"
    if payload[:4] == b"OggS":
        return "Ogg audio"
    if len(payload) >= 64:
        # PCM tends to be dense small-magnitude 16-bit samples rather than text.
        vals = struct.unpack_from("<32h", payload, 0)
        if all(abs(v) < 12000 for v in vals):
            return "possibly PCM audio (s16)"
    return None


class Framer:
    def __init__(self, who, logpath):
        self.who = who
        self.buf = b""
        self.seen = set()
        self.logpath = logpath

    def feed(self, data):
        self.buf += data
        while len(self.buf) >= HDR:
            mtype, plen = struct.unpack_from("<II", self.buf, 0)
            if plen > 64 * 1024 * 1024:
                log(f"{self.who}: unframed data, resyncing", self.logpath)
                self.buf = b""
                return
            if len(self.buf) < HDR + plen:
                return
            msg = self.buf[:HDR + plen]
            self.buf = self.buf[HDR + plen:]
            fields = struct.unpack_from("<32I", msg, 0)
            nz = {f"@{i*4}": v for i, v in enumerate(fields) if v and i > 1}
            known = KNOWN[self.who].get(mtype)
            first = mtype not in self.seen
            self.seen.add(mtype)
            records.append({"dir": self.who, "type": mtype, "plen": plen,
                            "fields": nz, "hex": msg[:HDR + min(plen, 256)].hex()})
            if known is None:
                # This is the point of the exercise.
                log(f"*** {self.who}: UNKNOWN TYPE {mtype}  payload={plen}B  {nz}", self.logpath)
                print(hexdump(msg[:HDR]), flush=True)
                if plen:
                    kind = looks_like(msg[HDR:])
                    log(f"    payload looks like: {kind or 'unrecognised'}", self.logpath)
                    print(hexdump(msg[HDR:]), flush=True)
            elif first:
                log(f"{self.who}: first {known} (type {mtype}, payload {plen}B) {nz}", self.logpath)
            elif known != "video (JPEG stripe)" and plen == 0 and mtype != 12:
                log(f"{self.who}: {known} {nz}", self.logpath)


def udp_responder(stop, real_host, logpath):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3)
    reply = None
    try:
        s.sendto(b"SPACEDESK-NET-CLIENT\x00", (real_host, PORT))
        reply, _ = s.recvfrom(65535)
    except OSError:
        pass
    s.close()
    if not reply:
        log("real server did not answer discovery — is it running?", logpath)
        return
    log(f"serving the real server's discovery response ({len(reply)}B)", logpath)
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    u.bind(("0.0.0.0", PORT))
    u.settimeout(0.5)
    while not stop.is_set():
        try:
            d, a = u.recvfrom(65535)
        except socket.timeout:
            continue
        if d.startswith(b"SPACEDESK"):
            for dst in ((a[0], a[1]), (a[0], PORT), ("255.255.255.255", PORT)):
                try:
                    u.sendto(reply, dst)
                except OSError:
                    pass
    u.close()


def pump(src, dst, framer, stop, logpath):
    try:
        while not stop.is_set():
            d = src.recv(65535)
            if not d:
                break
            framer.feed(d)
            dst.sendall(d)
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


PROMPTS = [
    (0,   "connect the phone to the server in the app"),
    (25,  ">>> NOW: tap the phone screen a few times (touch)"),
    (50,  ">>> NOW: drag your finger around (touch move)"),
    (75,  ">>> NOW: two-finger pinch / scroll"),
    (100, ">>> NOW: rotate the phone to landscape and back (orientation)"),
    (130, ">>> NOW: play audio on the Windows VM (audio path)"),
    (170, ">>> NOW: type on the phone's on-screen keyboard if the app offers one"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--seconds", type=int, default=210)
    a = ap.parse_args()
    host, _, p = a.server.partition(":")
    sport = int(p or PORT)
    logpath = os.path.join(HERE, "capture_all.log")
    open(logpath, "w").close()

    stop = threading.Event()
    threading.Thread(target=udp_responder, args=(stop, host, logpath), daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(4)
    srv.settimeout(0.5)
    log(f"relay ready: phone -> :{PORT} -> {host}:{sport}", logpath)

    def prompter():
        for at, text in PROMPTS:
            while time.time() - start < at and not stop.is_set():
                time.sleep(0.5)
            if stop.is_set():
                return
            log(text, logpath)
    threading.Thread(target=prompter, daemon=True).start()

    end = time.time() + a.seconds
    while time.time() < end and not stop.is_set():
        try:
            c, addr = srv.accept()
        except socket.timeout:
            continue
        log(f"=== viewer connected from {addr[0]} ===", logpath)
        try:
            up = socket.create_connection((host, sport), timeout=8)
        except OSError as e:
            log(f"!! cannot reach the real server: {e}", logpath)
            c.close()
            continue
        cf = Framer("viewer->server", logpath)
        sf = Framer("server->viewer", logpath)
        threading.Thread(target=pump, args=(c, up, cf, stop, logpath), daemon=True).start()
        threading.Thread(target=pump, args=(up, c, sf, stop, logpath), daemon=True).start()
    stop.set()
    time.sleep(0.5)

    with open(os.path.join(HERE, "capture_all.bin"), "w") as f:
        json.dump(records, f)
    types = {}
    for r in records:
        types.setdefault(r["dir"], {}).setdefault(r["type"], 0)
        types[r["dir"]][r["type"]] += 1
    log("=" * 66, logpath)
    log(f"message types seen: {json.dumps(types)}", logpath)
    unknown = [(d, t) for d, m in types.items() for t in m if t not in KNOWN[d]]
    log(f"NEW types to decode: {unknown if unknown else 'none'}", logpath)
    log(f"{len(records)} messages saved to capture_all.bin", logpath)


if __name__ == "__main__":
    main()
