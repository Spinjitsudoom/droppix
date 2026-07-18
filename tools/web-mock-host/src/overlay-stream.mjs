import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { probeMp4 } from "./mp4-stream.mjs";
import { createRgbEncoder } from "./h264-desktop.mjs";

/**
 * Loop an MP4, burn a server overlay into each RGB frame, re-encode to H.264.
 *
 * Media clock (not wall clock):
 *   video PTS = frameIndex * (1e6 / fps)   — stamped when the RGB frame is read
 *   audio PTS = sampleIndex * 1e6 / 48000 — stamped when the PCM chunk is read
 *
 * Dropped video frames (encoder busy) skip that PTS entirely — no delay queues,
 * no holding audio. Client syncs to the PTS on the wire.
 */
export function startOverlayStream({ mp4, desktop, onVideo, onAudio, onError }) {
  const info = probeMp4(mp4);
  const W = info.width;
  const H = info.height;
  const fps = info.fps;
  const frameBytes = W * H * 3;
  const frameUs = BigInt(Math.round(1_000_000 / fps));
  const CHUNK = 48000 * 0.02 * 4; // 20ms stereo s16le
  const chunkUs = 20_000n;

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "droppix-ov-"));
  const vFifo = path.join(tmp, "v.rgb");
  const aFifo = path.join(tmp, "a.pcm");
  spawnSync("mkfifo", [vFifo, aFifo]);

  let alive = true;
  let frameIndex = 0n;
  let audioPts = 0n;
  let droppedVideo = 0;
  let droppedAudio = 0;
  let lastVideoPts = 0n;
  let lastAudioPts = 0n;
  // Hold PCM by media PTS until the first video AU, then release from that PTS
  // onward (no fixed wall-clock delay — just PTS alignment).
  let audioFloorPts = null;
  const audioHold = []; // { ptsUs, pcm }
  // Audio may not run ahead of the latest emitted video PTS (encode pipeline
  // depth). Hold and release on each AU — PTS pacing, not a wall-clock delay.
  const AUDIO_SLACK_US = 40_000n; // one frame of slack

  const flushAudioHold = () => {
    if (audioFloorPts === null) return;
    const limit = lastVideoPts + AUDIO_SLACK_US;
    while (audioHold.length && audioHold[0].ptsUs <= limit) {
      const item = audioHold.shift();
      if (item.ptsUs < audioFloorPts) {
        droppedAudio++;
        continue;
      }
      lastAudioPts = item.ptsUs;
      onAudio(item.pcm, item.ptsUs);
    }
  };

  const enc = createRgbEncoder({
    w: W,
    h: H,
    fps,
    onAu: (au) => {
      lastVideoPts = au.ptsUs;
      if (audioFloorPts === null) audioFloorPts = au.ptsUs;
      onVideo(au);
      flushAudioHold();
    },
    onError,
  });

  const POOL = 8;
  const pool = Array.from({ length: POOL }, () => Buffer.allocUnsafe(frameBytes));
  let poolIdx = 0;
  let frame = pool[0];
  let filled = 0;
  let encReady = true;

  const vStream = fs.createReadStream(vFifo, { highWaterMark: frameBytes });
  vStream.on("data", (chunk) => {
    if (!alive) return;
    let off = 0;
    while (off < chunk.length) {
      const need = frameBytes - filled;
      const take = Math.min(need, chunk.length - off);
      chunk.copy(frame, filled, off, off + take);
      filled += take;
      off += take;
      if (filled === frameBytes) {
        filled = 0;
        const ptsUs = frameIndex * frameUs;
        frameIndex++;
        if (!encReady) {
          // Drop: media time advances, this PTS is never sent.
          droppedVideo++;
          continue;
        }
        try {
          desktop.overlayFrame(frame);
        } catch (e) {
          onError?.(`overlay: ${e}`);
        }
        if (!enc.write(frame, ptsUs)) {
          encReady = false;
          enc.onDrain(() => {
            encReady = true;
          });
        }
        poolIdx = (poolIdx + 1) % POOL;
        frame = pool[poolIdx];
      }
    }
  });
  vStream.on("error", (e) => onError?.(String(e)));

  let audioCarry = Buffer.alloc(0);
  const aStream = fs.createReadStream(aFifo);
  aStream.on("data", (c) => {
    if (!alive) return;
    audioCarry = Buffer.concat([audioCarry, c]);
    while (audioCarry.length >= CHUNK) {
      const pcm = audioCarry.subarray(0, CHUNK);
      audioCarry = audioCarry.subarray(CHUNK);
      const ptsUs = audioPts;
      audioPts += chunkUs;
      audioHold.push({ ptsUs, pcm: Buffer.from(pcm) });
      // Cap (~2s). If video stalls, drop the oldest — never invent delay.
      while (audioHold.length > 100) {
        audioHold.shift();
        droppedAudio++;
      }
      flushAudioHold();
    }
  });
  aStream.on("error", (e) => onError?.(String(e)));

  const args = [
    "-y", "-hide_banner", "-loglevel", "error",
    "-re", "-stream_loop", "-1", "-i", mp4,
    "-map", "0:v:0",
    "-vf", `scale=${W}:${H}:force_original_aspect_ratio=decrease,pad=${W}:${H}:(ow-iw)/2:(oh-ih)/2,fps=${fps},format=rgb24`,
    "-f", "rawvideo", vFifo,
    "-map", "0:a:0?", "-vn", "-ac", "2", "-ar", "48000", "-f", "s16le", aFifo,
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
    width: W,
    height: H,
    fps,
    mp4,
    get lipsync() {
      const skewMs =
        lastVideoPts && lastAudioPts
          ? Number(lastAudioPts - lastVideoPts) / 1000
          : 0;
      return {
        lastVideoPtsUs: lastVideoPts.toString(),
        lastAudioPtsUs: lastAudioPts.toString(),
        skewMs: Math.round(skewMs),
        droppedVideo,
        droppedAudio,
        frameIndex: Number(frameIndex),
      };
    },
    stop() {
      alive = false;
      try {
        proc.kill("SIGKILL");
      } catch {
        /* ignore */
      }
      enc.stop();
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
