import { spawn } from "node:child_process";

/**
 * Split an Annex-B byte stream into one-AU-per-callback, injecting SPS/PPS on
 * IDR. Returns a feed(chunk) function. Load-bearing: encoder must emit slices=1
 * so each VCL NAL is a complete access unit for WebCodecs.
 */
export function createAnnexBSplitter(onAu) {
  let buf = Buffer.alloc(0);
  const state = { sps: null, pps: null, vcl: [] };

  const flushVcl = () => {
    if (!state.vcl.length) return;
    const keyframe = state.vcl.some((n) => {
      const hdr = n[2] === 1 ? 3 : 4;
      return (n[hdr] & 0x1f) === 5;
    });
    const parts = [];
    if (keyframe) {
      if (state.sps) parts.push(state.sps);
      if (state.pps) parts.push(state.pps);
    }
    parts.push(...state.vcl);
    state.vcl = [];
    onAu({ keyframe, nal: new Uint8Array(Buffer.concat(parts)) });
  };

  return function feed(chunk) {
    buf = Buffer.concat([buf, chunk]);
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
      const nal = Buffer.from(buf.subarray(0, next));
      buf = buf.subarray(next);
      const hdrOff = nal[2] === 1 ? 3 : 4;
      if (nal.length <= hdrOff) continue;
      const nalType = nal[hdrOff] & 0x1f;
      if (nalType === 7) {
        flushVcl();
        state.sps = nal;
      } else if (nalType === 8) {
        state.pps = nal;
      } else if (nalType === 9) {
        flushVcl();
      } else if (nalType === 5 || nalType === 1) {
        if (nalType === 5 && state.vcl.length) flushVcl();
        state.vcl.push(nal);
        flushVcl();
      }
    }
  };
}

const X264_PARAMS =
  "repeat-headers=1:annexb=1:slices=1:sliced-threads=0:sync-lookahead=0:rc-lookahead=0";

/**
 * Push-driven RGB24 → Annex-B H.264 encoder. Caller feeds full frames with
 * write(); handles backpressure via the returned booleans + onDrain.
 */
export function createRgbEncoder({ w, h, fps, onAu, onError }) {
  const rate = Math.min(30, Math.max(10, Number(fps) || 24));
  const args = [
    "-hide_banner", "-loglevel", "error",
    "-fflags", "nobuffer", "-flags", "low_delay",
    "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", `${w}x${h}`,
    "-framerate", String(rate), "-i", "-",
    "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
    // ultrafast + zerolatency: emit each frame ASAP (PTS queue stays shallow).
    "-preset", "ultrafast",
    "-profile:v", "baseline", "-level", "3.1", "-tune", "zerolatency",
    "-crf", "23", "-maxrate", "4500k", "-bufsize", "9000k",
    "-bf", "0", "-g", String(rate), "-keyint_min", "1",
    "-x264-params", X264_PARAMS,
    "-bsf:v", "h264_mp4toannexb",
    "-flush_packets", "1", "-f", "h264", "-",
  ];
  const proc = spawn("ffmpeg", args, { stdio: ["pipe", "pipe", "pipe"] });
  // Media PTS travels with each written frame; emit it with the matching AU
  // (same pattern as host SoftwareEncoder pts_map_).
  const ptsQ = [];
  const feed = createAnnexBSplitter((au) => {
    const ptsUs = ptsQ.length ? ptsQ.shift() : 0n;
    onAu({ ...au, ptsUs });
  });
  let alive = true;
  proc.stderr.on("data", (d) => {
    const s = d.toString().trim();
    if (s) onError?.(s);
  });
  proc.on("error", (e) => onError?.(String(e)));
  proc.stdin.on("error", (e) => {
    if (e?.code !== "EPIPE") onError?.(String(e));
    alive = false;
  });
  proc.on("exit", () => {
    alive = false;
  });
  proc.stdout.on("data", (c) => feed(c));

  return {
    /** @param {Buffer} frame @param {bigint} ptsUs media presentation time */
    write(frame, ptsUs = 0n) {
      if (!alive || proc.stdin.destroyed || !proc.stdin.writable) return false;
      ptsQ.push(typeof ptsUs === "bigint" ? ptsUs : BigInt(ptsUs));
      return proc.stdin.write(frame);
    },
    onDrain(cb) {
      proc.stdin.once("drain", cb);
    },
    stop() {
      alive = false;
      ptsQ.length = 0;
      try {
        proc.stdin.end();
      } catch {
        /* ignore */
      }
      try {
        proc.kill("SIGKILL");
      } catch {
        /* ignore */
      }
    },
  };
}

/**
 * Encode a mock desktop's RGB24 frames to Annex-B H.264 via ffmpeg stdin.
 * Forces a single slice per frame so WebCodecs gets one AU per picture.
 */
export function startDesktopEncoder({ desktop, fps, onAu, onError }) {
  const w = desktop.width;
  const h = desktop.height;
  const rate = Math.min(20, Math.max(10, Number(fps) || 15));
  const frameBytes = w * h * 3;
  const rgb = Buffer.alloc(frameBytes);

  const args = [
    "-hide_banner",
    "-loglevel",
    "error",
    "-f",
    "rawvideo",
    "-pix_fmt",
    "rgb24",
    "-s",
    `${w}x${h}`,
    "-framerate",
    String(rate),
    "-i",
    "-",
    "-an",
    "-c:v",
    "libx264",
    "-pix_fmt",
    "yuv420p",
    "-profile:v",
    "baseline",
    "-level",
    "3.1",
    "-tune",
    "zerolatency",
    "-bf",
    "0",
    "-g",
    String(rate),
    "-keyint_min",
    String(rate),
    // One slice per frame is load-bearing for WebCodecs EncodedVideoChunk = one AU.
    "-x264-params",
    "repeat-headers=1:annexb=1:slices=1:sliced-threads=0:sync-lookahead=0:rc-lookahead=0",
    "-bsf:v",
    "h264_mp4toannexb",
    "-f",
    "h264",
    "-",
  ];

  const proc = spawn("ffmpeg", args, { stdio: ["pipe", "pipe", "pipe"] });
  let buf = Buffer.alloc(0);
  const state = { sps: null, pps: null, vcl: [] };
  let alive = true;
  const t0 = Date.now();
  let writing = false;

  proc.stderr.on("data", (d) => {
    const s = d.toString().trim();
    if (s) onError?.(s);
  });
  proc.on("error", (e) => onError?.(String(e)));
  proc.stdin.on("error", (e) => {
    if (e?.code !== "EPIPE") onError?.(String(e));
    alive = false;
  });
  proc.on("exit", () => {
    alive = false;
  });

  const flushVcl = () => {
    if (!state.vcl.length) return;
    const keyframe = state.vcl.some((n) => {
      const hdr = n[2] === 1 ? 3 : 4;
      return (n[hdr] & 0x1f) === 5;
    });
    const parts = [];
    if (keyframe) {
      if (state.sps) parts.push(state.sps);
      if (state.pps) parts.push(state.pps);
    }
    parts.push(...state.vcl);
    state.vcl = [];
    onAu({ keyframe, nal: new Uint8Array(Buffer.concat(parts)) });
  };

  proc.stdout.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
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
      const nal = Buffer.from(buf.subarray(0, next));
      buf = buf.subarray(next);
      const hdrOff = nal[2] === 1 ? 3 : 4;
      if (nal.length <= hdrOff) continue;
      const nalType = nal[hdrOff] & 0x1f;
      if (nalType === 7) {
        flushVcl();
        state.sps = nal;
      } else if (nalType === 8) {
        state.pps = nal;
      } else if (nalType === 9) {
        flushVcl();
      } else if (nalType === 5 || nalType === 1) {
        // New IDR starts a new AU.
        if (nalType === 5 && state.vcl.length) flushVcl();
        state.vcl.push(nal);
        // With slices=1, one VCL = one AU.
        flushVcl();
      }
      // ignore SEI (6) etc.
    }
  });

  const interval = setInterval(() => {
    if (!alive || proc.stdin.destroyed || !proc.stdin.writable || writing) return;
    const tSec = (Date.now() - t0) / 1000;
    desktop.render(rgb, tSec);
    writing = !proc.stdin.write(rgb);
    if (writing) {
      proc.stdin.once("drain", () => {
        writing = false;
      });
    }
  }, Math.round(1000 / rate));

  return {
    width: w,
    height: h,
    stop() {
      clearInterval(interval);
      alive = false;
      try {
        proc.stdin.end();
      } catch {
        /* ignore */
      }
      try {
        proc.kill("SIGKILL");
      } catch {
        /* ignore */
      }
    },
  };
}
