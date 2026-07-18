import { contentBox, type FitMode } from "./fit.ts";

export class VideoPipeline {
  private decoder: VideoDecoder | null = null;
  private configured = false;
  private vw = 0;
  private vh = 0;
  private frames = 0;
  private lastFpsAt = performance.now();
  private fps = 0;
  private fit: FitMode = "contain";

  constructor(
    private canvas: HTMLCanvasElement,
    private opts: { flip: boolean; brightness: number; contrast: number },
  ) {}

  get size() {
    return { w: this.vw, h: this.vh };
  }
  get currentFps() {
    return this.fps;
  }

  setFit(mode: FitMode) {
    this.fit = mode;
  }

  setAdjust(flip: boolean, brightness: number, contrast: number) {
    this.opts = { flip, brightness, contrast };
  }

  async submit(keyframe: boolean, nal: Uint8Array): Promise<void> {
    if (typeof VideoDecoder === "undefined") {
      throw new Error("WebCodecs VideoDecoder not available");
    }
    if (!this.decoder || this.decoder.state === "closed") {
      this.decoder = new VideoDecoder({
        output: (frame) => this.draw(frame),
        error: (e) => console.error("VideoDecoder", e),
      });
      this.configured = false;
    }
    if (!this.configured) {
      if (!keyframe) return;
      this.decoder.configure({
        codec: "avc1.42E01E",
        optimizeForLatency: true,
      });
      this.configured = true;
    }
    const chunk = new EncodedVideoChunk({
      type: keyframe ? "key" : "delta",
      timestamp: performance.now() * 1000,
      data: nal,
    });
    try {
      this.decoder.decode(chunk);
    } catch (e) {
      console.warn("decode", e);
      if (keyframe) this.configured = false;
    }
  }

  private draw(frame: VideoFrame) {
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
    const now = performance.now();
    if (now - this.lastFpsAt >= 1000) {
      this.fps = this.frames;
      this.frames = 0;
      this.lastFpsAt = now;
    }
  }

  close() {
    try {
      this.decoder?.close();
    } catch {
      /* ignore */
    }
    this.decoder = null;
    this.configured = false;
  }
}
