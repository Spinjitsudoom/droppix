import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const LOOP = path.join(__dirname, "..", "assets", "loop.pcm");
const GEN = path.join(__dirname, "..", "scripts", "gen-audio.sh");

/** 20ms of s16le stereo @ 48k = 48000*0.02*2*2 = 3840 bytes */
export const CHUNK_BYTES = 48000 * 0.02 * 2 * 2;

export function loadAudioLoop() {
  if (!fs.existsSync(LOOP) || fs.statSync(LOOP).size < CHUNK_BYTES * 10) {
    console.log("generating mock speech+music PCM…");
    const r = spawnSync("bash", [GEN], { stdio: "inherit" });
    if (r.status !== 0 || !fs.existsSync(LOOP)) {
      console.warn("audio gen failed; falling back to silent chunks");
      return Buffer.alloc(CHUNK_BYTES);
    }
  }
  const buf = fs.readFileSync(LOOP);
  // Ensure length is a multiple of chunk size
  const n = Math.floor(buf.length / CHUNK_BYTES) * CHUNK_BYTES;
  return buf.subarray(0, Math.max(n, CHUNK_BYTES));
}

export function createAudioCursor(loopBuf) {
  let off = 0;
  return {
    nextChunk() {
      if (loopBuf.length < CHUNK_BYTES) return loopBuf;
      if (off + CHUNK_BYTES > loopBuf.length) off = 0;
      const slice = loopBuf.subarray(off, off + CHUNK_BYTES);
      off += CHUNK_BYTES;
      return slice;
    },
  };
}
