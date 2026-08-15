import { contentBox, type FitMode } from "./fit.ts";
import { avcCodecString } from "./avc-codec.ts";
import type { VideoRenderer } from "./video-renderer.ts";

/**
 * H.264 → canvas. Decoded frames are held and painted against a media clock
 * (audio wire PTS) so lipsync follows stream timestamps; drops are skips in
 * that timeline, not something we invent delays for.
 */
export class VideoPipeline implements VideoRenderer {
  private decoder: VideoDecoder | null = null;
  private configured = false;
  private closed = false;
  private dropUntilKey = false;
  private vw = 0;
  private vh = 0;
  private frames = 0;
  private painted = 0;
  private received = 0;
  private lastFpsAt = performance.now();
  private fps = 0;
  // Arrival-rate counter (frames submitted/sec), separate from painted fps so the HUD can
  // show in-vs-out: in≈out but low ⇒ decode-bound; in high, out low ⇒ paint-bound.
  private recv = 0;
  private recvFps = 0;
  private recvAt = performance.now();
  private fit: FitMode = "contain";
  private lastError = "";
  private pending: VideoFrame[] = [];
  private raf = 0;
  /** Returns media PTS in microseconds, or null to paint ASAP. */
  private getClock: (() => number | null) | null = null;
  private requestKeyframe: (() => void) | null = null;

  constructor(
    private canvas: HTMLCanvasElement,
    private opts: { flip: boolean; brightness: number; contrast: number },
    private onInfo?: (msg: string) => void,
  ) {}

  get size() {
    return { w: this.vw, h: this.vh };
  }
  get currentFps() {
    return this.fps;
  }
  /** Frames arriving from the network per second (before decode/paint). */
  get inFps() {
    return this.recvFps;
  }
  /** Decode backlog (frames queued in the VideoDecoder). */
  get decodeQueue() {
    return this.decoder?.decodeQueueSize ?? 0;
  }
  /** Paint backlog (decoded frames waiting for their presentation slot). */
  get paintQueue() {
    return this.pending.length;
  }
  get hasPainted() {
    return this.painted > 0;
  }
  get stats() {
    return { received: this.received, painted: this.painted, fps: this.fps, lastError: this.lastError };
  }

  setFit(mode: FitMode) {
    this.fit = mode;
  }

  setAdjust(flip: boolean, brightness: number, contrast: number) {
    this.opts = { flip, brightness, contrast };
  }

  /** Ask the host for an immediate IDR (set by main.ts once the transport exists). */
  setKeyframeRequester(fn: (() => void) | null) {
    this.requestKeyframe = fn;
  }

  /** Master clock for presentation (typically audio wire PTS). */
  setClock(fn: (() => number | null) | null) {
    this.getClock = fn;
  }

  submit(keyframe: boolean, nal: Uint8Array, ptsUs: bigint): void {
    this.closed = false;
    this.received++;
    this.recv++;
    const nowRecv = performance.now();
    if (nowRecv - this.recvAt >= 1000) {
      // Scale by the real window: at low fps it stretches past 1s and a raw count lies.
      this.recvFps = Math.round((this.recv * 1000) / (nowRecv - this.recvAt));
      this.recv = 0;
      this.recvAt = nowRecv;
    }
    if (typeof VideoDecoder === "undefined") {
      this.lastError = "WebCodecs VideoDecoder missing - use Chromium";
      this.onInfo?.(this.lastError);
      return;
    }
    if (!this.decoder || this.decoder.state === "closed") {
      this.decoder = new VideoDecoder({
        output: (frame) => this.onDecoded(frame),
        error: (e) => {
          this.lastError = String(e?.message || e);
          this.onInfo?.(`VideoDecoder: ${this.lastError}`);
          this.configured = false;
          try {
            this.decoder?.close();
          } catch {
            /* ignore */
          }
          this.decoder = null;
        },
      });
      this.configured = false;
      this.dropUntilKey = false;
    }
    if (!this.configured) {
      if (!keyframe) return;
      // Declare the ACTUAL stream profile/level (from the in-band SPS), not a fixed
      // guess — Chrome refuses to decode when the codec string understates the stream
      // (e.g. a hardcoded Baseline-3.1 vs the host's High/L4.0 at 1080p => black screen).
      const codec = avcCodecString(nal) ?? "avc1.42E01F";
      this.decoder.configure({
        codec,
        optimizeForLatency: true,
      });
      this.configured = true;
    }

    if (keyframe) {
      this.dropUntilKey = false;
    } else if (this.dropUntilKey) {
      return;
    }

    if (this.decoder.decodeQueueSize > 20 && !keyframe) {
      // Backlogged: drop to the next keyframe, but ASK for one — otherwise we freeze
      // until the host's scheduled IDR (up to 2s at the default GOP).
      this.startDropping();
      return;
    }

    const chunk = new EncodedVideoChunk({
      type: keyframe ? "key" : "delta",
      timestamp: Number(ptsUs),
      data: nal,
    });
    try {
      this.decoder.decode(chunk);
    } catch (e) {
      this.lastError = String((e as Error)?.message || e);
      this.onInfo?.(`decode: ${this.lastError}`);
      this.configured = false;
      this.startDropping();
    }
  }

  /** Enter drop-until-keyframe and ask the host to send one now (once per episode). */
  private startDropping() {
    if (!this.dropUntilKey) this.requestKeyframe?.();
    this.dropUntilKey = true;
  }

  private onDecoded(frame: VideoFrame) {
    if (this.closed) {
      frame.close();
      return;
    }
    this.pending.push(frame);
    while (this.pending.length > 12) {
      this.pending.shift()!.close();
    }
    this.schedulePaint();
  }

  private schedulePaint() {
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => {
      this.raf = 0;
      this.paintDue();
    });
  }

  private paintDue() {
    if (this.closed) return;
    const clock = this.getClock?.() ?? null;
    if (clock == null) {
      // No audio clock: paint latest, drop the rest.
      while (this.pending.length > 1) this.pending.shift()!.close();
      const f = this.pending.shift();
      if (f) this.draw(f);
      return;
    }
    // Paint the newest frame whose PTS <= media clock; drop older ones.
    let best = -1;
    for (let i = 0; i < this.pending.length; i++) {
      if (this.pending[i]!.timestamp <= clock) best = i;
    }
    if (best < 0) {
      // Video ahead of audio — wait.
      this.schedulePaint();
      return;
    }
    for (let i = 0; i < best; i++) this.pending.shift()!.close();
    const f = this.pending.shift();
    if (f) this.draw(f);
    if (this.pending.length) this.schedulePaint();
  }

  private draw(frame: VideoFrame) {
    if (this.closed) {
      frame.close();
      return;
    }
    this.vw = frame.displayWidth;
    this.vh = frame.displayHeight;
    const ctx = this.canvas.getContext("2d");
    if (!ctx) {
      frame.close();
      return;
    }
    const dpr = window.devicePixelRatio || 1;
    const cssW = this.canvas.clientWidth;
    const cssH = this.canvas.clientHeight;
    const pw = Math.max(1, Math.round(cssW * dpr));
    const ph = Math.max(1, Math.round(cssH * dpr));
    if (this.canvas.width !== pw || this.canvas.height !== ph) {
      this.canvas.width = pw;
      this.canvas.height = ph;
    }
    const box = contentBox(pw, ph, this.vw, this.vh, this.fit);
    ctx.save();
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, pw, ph);
    ctx.beginPath();
    ctx.rect(box.x, box.y, box.w, box.h);
    ctx.clip();
    // ctx.filter forces a slow (often CPU) raster path in Chrome for EVERY drawImage,
    // even when the filter is the identity brightness(1) contrast(1). On low-end GPUs
    // that pins playback at a few fps, so only engage it when a real adjustment is set.
    if (this.opts.brightness !== 1 || this.opts.contrast !== 1) {
      ctx.filter = `brightness(${this.opts.brightness}) contrast(${this.opts.contrast})`;
    }
    if (this.opts.flip) {
      ctx.translate(box.x + box.w, box.y);
      ctx.scale(-1, 1);
      ctx.drawImage(frame, 0, 0, box.w, box.h);
    } else {
      ctx.drawImage(frame, box.x, box.y, box.w, box.h);
    }
    ctx.restore();
    frame.close();
    this.frames++;
    this.painted++;
    const now = performance.now();
    if (now - this.lastFpsAt >= 1000) {
      this.fps = Math.round((this.frames * 1000) / (now - this.lastFpsAt));
      this.frames = 0;
      this.lastFpsAt = now;
    }
  }

  close() {
    this.closed = true;
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
    for (const f of this.pending) f.close();
    this.pending = [];
    try {
      this.decoder?.close();
    } catch {
      /* ignore */
    }
    this.decoder = null;
    this.configured = false;
    this.dropUntilKey = false;
    this.painted = 0;
    this.received = 0;
    const ctx = this.canvas.getContext("2d");
    if (ctx) {
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.filter = "none";
      ctx.fillStyle = "#000";
      ctx.fillRect(0, 0, this.canvas.width || 1, this.canvas.height || 1);
      ctx.restore();
    }
  }
}
