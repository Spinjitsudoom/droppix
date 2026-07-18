import { contentBox, type FitMode } from "./fit.ts";

/**
 * H.264 → canvas. Decoded frames are held and painted against a media clock
 * (audio wire PTS) so lipsync follows stream timestamps; drops are skips in
 * that timeline, not something we invent delays for.
 */
export class VideoPipeline {
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
  private fit: FitMode = "contain";
  private lastError = "";
  private pending: VideoFrame[] = [];
  private raf = 0;
  /** Returns media PTS in microseconds, or null to paint ASAP. */
  private getClock: (() => number | null) | null = null;

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

  /** Master clock for presentation (typically audio wire PTS). */
  setClock(fn: (() => number | null) | null) {
    this.getClock = fn;
  }

  submit(keyframe: boolean, nal: Uint8Array, ptsUs: bigint): void {
    this.closed = false;
    this.received++;
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
      this.decoder.configure({
        codec: "avc1.42E01F",
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
      this.dropUntilKey = true;
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
      this.dropUntilKey = true;
    }
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
    ctx.filter = `brightness(${this.opts.brightness}) contrast(${this.opts.contrast})`;
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
      this.fps = this.frames;
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
