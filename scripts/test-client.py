#!/usr/bin/env python3
"""Connect to droppix_stream, do the HELLO/CONFIG handshake, and write the
received H.264 Annex-B stream (CONFIG extradata first, then each VIDEO NAL) to
stdout. Pipe to a player or ffprobe:

    python3 scripts/test-client.py 27000 1920 1080 | ffplay -fflags nobuffer -
    python3 scripts/test-client.py 27000 1920 1080 > out.h264   # then ffprobe out.h264
"""
import socket, struct, sys

HELLO, CONFIG, VIDEO, PING, PONG, BYE = 1, 2, 3, 4, 5, 6

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 27000
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 1280
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 720
    out = sys.stdout.buffer

    s = socket.create_connection(("127.0.0.1", port))
    s.sendall(struct.pack(">IB", 1 + 12, HELLO) + struct.pack(">III", w, h, 320))

    buf = b""
    def read_msg():
        nonlocal buf
        while len(buf) < 4:
            d = s.recv(65536)
            if not d: return None
            buf += d
        (length,) = struct.unpack(">I", buf[:4])
        while len(buf) < 4 + length:
            d = s.recv(65536)
            if not d: return None
            buf += d
        mtype = buf[4]
        body = buf[5:4 + length]
        buf = buf[4 + length:]
        return mtype, body

    while True:
        msg = read_msg()
        if msg is None: break
        mtype, body = msg
        if mtype == CONFIG:
            w2, h2, fps, edlen = struct.unpack(">IIII", body[:16])
            extradata = body[16:16 + edlen]
            sys.stderr.write(f"CONFIG {w2}x{h2}@{fps} extradata={len(extradata)}B\n")
            if extradata:
                out.write(extradata); out.flush()
        elif mtype == VIDEO:
            # body = u64 pts, u8 keyframe, then NAL
            nal = body[9:]
            out.write(nal); out.flush()
        elif mtype == BYE:
            break

if __name__ == "__main__":
    main()
