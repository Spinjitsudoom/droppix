import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const TOOL_ROOT = path.resolve(__dirname, "..");
const DEFAULT_MP4 = path.join(TOOL_ROOT, "assets", "sample.mp4");

export function resolveSampleMp4() {
  const fromEnv = process.env.DROPPIX_MOCK_MP4;
  if (fromEnv && fs.existsSync(fromEnv)) return path.resolve(fromEnv);
  if (fs.existsSync(DEFAULT_MP4)) return DEFAULT_MP4;
  const r = spawnSync("bash", [path.join(TOOL_ROOT, "scripts/fetch-sample.sh")], {
    stdio: "inherit",
  });
  if (r.status !== 0 || !fs.existsSync(DEFAULT_MP4)) {
    throw new Error(
      "No sample MP4. Set DROPPIX_MOCK_MP4 or run scripts/fetch-sample.sh",
    );
  }
  return DEFAULT_MP4;
}

export function probeMp4(mp4) {
  const r = spawnSync(
    "ffprobe",
    [
      "-v",
      "error",
      "-select_streams",
      "v:0",
      "-show_entries",
      "stream=width,height,r_frame_rate",
      "-of",
      "csv=p=0:s=x",
      mp4,
    ],
    { encoding: "utf8" },
  );
  const line = (r.stdout || "").trim();
  // e.g. 1280x720x24/1
  const m = line.match(/(\d+)x(\d+)x(\d+)(?:\/(\d+))?/);
  let width = 1280;
  let height = 720;
  let fps = 24;
  if (m) {
    width = Number(m[1]);
    height = Number(m[2]);
    const num = Number(m[3]);
    const den = Number(m[4] || 1);
    if (num > 0 && den > 0) fps = Math.max(1, Math.round(num / den));
  }
  // Even dims for yuv420 re-encode
  width = Math.max(64, Math.floor(width / 2) * 2);
  height = Math.max(64, Math.floor(height / 2) * 2);
  fps = Math.min(30, Math.max(12, fps));
  return { width, height, fps };
}

/**
 * Loop an MP4 at realtime: Annex-B H.264 + s16le 48k stereo from the same demux
 * (one ffmpeg, two fifos) so A/V stay aligned for lipsync checks.
 */
export function startMp4Stream({ mp4, onVideo, onAudio, onError }) {
  const info = probeMp4(mp4);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "droppix-mp4-"));
  const vFifo = path.join(tmp, "v.h264");
  const aFifo = path.join(tmp, "a.pcm");
  spawnSync("mkfifo", [vFifo, aFifo]);

  let alive = true;
  let vBuf = Buffer.alloc(0);
  const state = { sps: null, pps: null };
  let audioCarry = Buffer.alloc(0);
  const CHUNK = 48000 * 0.02 * 4; // 20ms stereo s16le

  const flushVcl = (parts, keyframe) => {
    onVideo({ keyframe, nal: new Uint8Array(Buffer.concat(parts)) });
  };

  const feedNals = (chunk) => {
    vBuf = Buffer.concat([vBuf, chunk]);
    while (vBuf.length >= 4) {
      let sc = -1;
      let scLen = 0;
      for (let i = 0; i + 3 < vBuf.length; i++) {
        if (vBuf[i] === 0 && vBuf[i + 1] === 0 && vBuf[i + 2] === 0 && vBuf[i + 3] === 1) {
          sc = i;
          scLen = 4;
          break;
        }
        if (vBuf[i] === 0 && vBuf[i + 1] === 0 && vBuf[i + 2] === 1) {
          sc = i;
          scLen = 3;
          break;
        }
      }
      if (sc < 0) {
        vBuf = Buffer.alloc(0);
        break;
      }
      if (sc > 0) vBuf = vBuf.subarray(sc);
      let next = -1;
      for (let j = scLen; j + 3 < vBuf.length; j++) {
        if (vBuf[j] === 0 && vBuf[j + 1] === 0 && vBuf[j + 2] === 0 && vBuf[j + 3] === 1) {
          next = j;
          break;
        }
        if (vBuf[j] === 0 && vBuf[j + 1] === 0 && vBuf[j + 2] === 1) {
          next = j;
          break;
        }
      }
      if (next < 0) break;
      const nal = Buffer.from(vBuf.subarray(0, next));
      vBuf = vBuf.subarray(next);
      const hdrOff = nal[2] === 1 ? 3 : 4;
      if (nal.length <= hdrOff) continue;
      const nalType = nal[hdrOff] & 0x1f;
      if (nalType === 7) state.sps = nal;
      else if (nalType === 8) state.pps = nal;
      else if (nalType === 5 || nalType === 1) {
        const parts = [];
        if (nalType === 5) {
          if (state.sps) parts.push(state.sps);
          if (state.pps) parts.push(state.pps);
        }
        parts.push(nal);
        flushVcl(parts, nalType === 5);
      }
    }
  };

  // Open readers first (async) so ffmpeg can open writers without deadlock.
  const vStream = fs.createReadStream(vFifo);
  const aStream = fs.createReadStream(aFifo);
  vStream.on("data", (c) => {
    if (alive) feedNals(c);
  });
  aStream.on("data", (c) => {
    if (!alive) return;
    audioCarry = Buffer.concat([audioCarry, c]);
    while (audioCarry.length >= CHUNK) {
      onAudio(audioCarry.subarray(0, CHUNK));
      audioCarry = audioCarry.subarray(CHUNK);
    }
  });
  vStream.on("error", (e) => onError?.(String(e)));
  aStream.on("error", (e) => onError?.(String(e)));

  const args = [
    "-y",
    "-hide_banner",
    "-loglevel",
    "error",
    "-re",
    "-stream_loop",
    "-1",
    "-i",
    mp4,
    "-map",
    "0:v:0",
    "-vf",
    `scale=${info.width}:${info.height}:force_original_aspect_ratio=decrease,pad=${info.width}:${info.height}:(ow-iw)/2:(oh-ih)/2`,
    "-c:v",
    "libx264",
    "-pix_fmt",
    "yuv420p",
    "-profile:v",
    "baseline",
    // 3.1 covers 720p30; 3.0 does not (x264 would silently bump it anyway).
    "-level",
    "3.1",
    "-tune",
    "zerolatency",
    "-crf",
    "20",
    "-maxrate",
    "4500k",
    "-bufsize",
    "9000k",
    "-bf",
    "0",
    "-g",
    String(info.fps),
    "-keyint_min",
    String(info.fps),
    "-x264-params",
    "repeat-headers=1:annexb=1:slices=1:sliced-threads=0:scenecut=0",
    "-bsf:v",
    "h264_mp4toannexb",
    "-f",
    "h264",
    vFifo,
    "-map",
    "0:a:0?",
    "-vn",
    "-ac",
    "2",
    "-ar",
    "48000",
    "-f",
    "s16le",
    aFifo,
  ];

  const proc = spawn("ffmpeg", args, { stdio: ["ignore", "ignore", "pipe"] });
  proc.stderr.on("data", (d) => {
    const s = d.toString().trim();
    if (s) onError?.(s);
  });
  proc.on("error", (e) => onError?.(String(e)));
  proc.on("exit", (code) => {
    if (alive && code && code !== 0) onError?.(`ffmpeg exited ${code}`);
  });

  return {
    ...info,
    mp4,
    stop() {
      alive = false;
      try {
        proc.kill("SIGKILL");
      } catch {
        /* ignore */
      }
      try {
        vStream.destroy();
        aStream.destroy();
      } catch {
        /* ignore */
      }
      try {
        fs.rmSync(tmp, { recursive: true, force: true });
      } catch {
        /* ignore */
      }
    },
  };
}
