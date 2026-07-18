import { spawn } from "node:child_process";

function even(n, fallback) {
  const v = Number(n) || fallback;
  const e = Math.max(64, Math.floor(v / 2) * 2);
  return e;
}

/**
 * Spawn ffmpeg lavfi testsrc2 → Annex-B H.264 (baseline).
 * Emits complete access units via onAu({ keyframe, nal }).
 */
export function startH264Pipe({ width, height, fps, onAu, onError }) {
  const w = even(width, 1280);
  const h = even(height, 720);
  const rate = Math.min(30, Math.max(5, Number(fps) || 30));

  const args = [
    "-hide_banner",
    "-loglevel",
    "error",
    "-re",
    "-f",
    "lavfi",
    "-i",
    // Moving test pattern + grid (no font deps). Clear "alive" signal for blank-screen debugging.
    `testsrc2=size=${w}x${h}:rate=${rate},drawgrid=w=iw/10:h=ih/10:t=2:c=white@0.35`,
    "-c:v",
    "libx264",
    "-pix_fmt",
    "yuv420p",
    "-profile:v",
    "baseline",
    "-level",
    "3.0",
    "-tune",
    "zerolatency",
    "-bf",
    "0",
    // Frequent IDRs + single slice = fewer WebCodecs glitches.
    "-g",
    String(Math.max(8, Math.floor(rate / 2))),
    "-keyint_min",
    String(Math.max(8, Math.floor(rate / 2))),
    "-x264-params",
    "repeat-headers=1:annexb=1:slices=1:sliced-threads=0:scenecut=0",
    "-bsf:v",
    "h264_mp4toannexb",
    "-f",
    "h264",
    "-",
  ];
  const proc = spawn("ffmpeg", args, { stdio: ["ignore", "pipe", "pipe"] });
  let buf = Buffer.alloc(0);
  const state = { sps: null, pps: null, pending: [] };

  proc.stderr.on("data", (d) => {
    const s = d.toString().trim();
    if (s) onError?.(s);
  });
  proc.on("error", (e) => onError?.(String(e)));
  proc.on("exit", (code) => {
    if (code && code !== 0) onError?.(`ffmpeg exited ${code}`);
  });

  const emitPending = (keyframe) => {
    if (!state.pending.length) return;
    const parts = [];
    if (keyframe) {
      if (state.sps) parts.push(state.sps);
      if (state.pps) parts.push(state.pps);
    }
    parts.push(...state.pending);
    state.pending = [];
    const au = Buffer.concat(parts);
    onAu({ keyframe, nal: new Uint8Array(au) });
  };

  proc.stdout.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    const nals = [];
    while (buf.length >= 4) {
      let sc = -1;
      let scLen = 0;
      for (let i = 0; i + 3 < buf.length; i++) {
        if (buf[i] === 0 && buf[i + 1] === 0 && buf[i + 2] === 0 && buf[i + 3] === 1) {
          sc = i;
          scLen = 4;
          break;
        }
        if (buf[i] === 0 && buf[i + 1] === 0 && buf[i + 2] === 1) {
          sc = i;
          scLen = 3;
          break;
        }
      }
      if (sc < 0) {
        buf = Buffer.alloc(0);
        break;
      }
      if (sc > 0) buf = buf.subarray(sc);

      let next = -1;
      for (let j = scLen; j + 3 < buf.length; j++) {
        if (buf[j] === 0 && buf[j + 1] === 0 && buf[j + 2] === 0 && buf[j + 3] === 1) {
          next = j;
          break;
        }
        if (buf[j] === 0 && buf[j + 1] === 0 && buf[j + 2] === 1) {
          next = j;
          break;
        }
      }
      if (next < 0) break;
      nals.push(Buffer.from(buf.subarray(0, next)));
      buf = buf.subarray(next);
    }

    for (const nal of nals) {
      const hdrOff = nal[2] === 1 ? 3 : 4;
      if (nal.length <= hdrOff) continue;
      const nalType = nal[hdrOff] & 0x1f;
      if (nalType === 7) {
        state.sps = nal;
        continue;
      }
      if (nalType === 8) {
        state.pps = nal;
        continue;
      }
      if (nalType === 9) {
        // AUD: boundary before next AU
        continue;
      }
      if (nalType === 5 || nalType === 1) {
        // One VCL NAL = one AU for baseline (no B-frames).
        state.pending.push(nal);
        emitPending(nalType === 5);
      }
    }
  });

  return {
    width: w,
    height: h,
    stop() {
      try {
        proc.kill("SIGKILL");
      } catch {
        /* ignore */
      }
    },
  };
}
