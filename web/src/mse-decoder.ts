import type { FitMode } from "./fit.ts";
import { avcCodecString } from "./avc-codec.ts";
import {
  parseAnnexB,
  extractParamSets,
  annexBToAvcc,
  buildInit,
  buildSegment,
} from "./fmp4.ts";
import type { VideoRenderer } from "./video-renderer.ts";

/**
 * H.264 → fragmented-MP4 → <video> via Media Source Extensions.
 *
 * Muxes each incoming access unit into an fMP4 media segment and appends it to a
 * SourceBuffer, so the browser's NATIVE (typically hardware) H.264 decoder and the
 * compositor render the frame — no WebCodecs-in-JS, no per-frame canvas copy. On
 * low-end devices that's the difference between a few fps and full rate.
 *
 * Audio stays on the separate PCM/WebAudio path; the <video> is muted and free-runs,
 * with a small live-edge chase to bound latency. One frame is held so each segment
 * gets an exact (gapless) duration from the next frame's PTS.
 */
export class MseVideoPipeline implements VideoRenderer {
  private ms: MediaSource | null = null;
  private sb: SourceBuffer | null = null;
  private codec = "";
  private sps?: Uint8Array;
  private pps?: Uint8Array;
  private started = false;
  private seq = 1;
  private firstPtsUs: number | null = null;
  private prev: { sample: Uint8Array; ptsUs: number; keyframe: boolean } | null = null;
  private queue: Uint8Array[] = [];
  private busy = false;
  private closed = false;
  private lastError = "";
  private requestKeyframe: (() => void) | null = null;
  private displayW = 1280;
  private displayH = 720;

  // instrumentation (windowed rates + cumulative counts for the debug hook)
  private totalRecv = 0;
  private recvWin = 0;
  private recvFps = 0;
  private recvAt = performance.now();
  private appended = 0;
  private lastRendered = 0;
  private outFps = 0;

  constructor(
    private video: HTMLVideoElement,
    private opts: { flip: boolean; brightness: number; contrast: number },
    private onInfo?: (msg: string) => void,
  ) {
    this.video.muted = true;
    this.video.playsInline = true;
    this.applyAdjust();
  }

  get size() {
    return { w: this.video.videoWidth || this.displayW, h: this.video.videoHeight || this.displayH };
  }
  get currentFps() {
    return this.outFps;
  }
  get inFps() {
    return this.recvFps;
  }
  get decodeQueue() {
    return this.queue.length;
  }
  get paintQueue() {
    // seconds buffered ahead of playback ≈ latency; a proxy for the canvas path's queue.
    try {
      const b = this.video.buffered;
      if (b.length) return Math.round((b.end(b.length - 1) - this.video.currentTime) * 100) / 100;
    } catch {
      /* ignore */
    }
    return 0;
  }
  get stats() {
    const q = this.video.getVideoPlaybackQuality?.();
    return {
      received: this.totalRecv,
      painted: q ? q.totalVideoFrames : this.appended,
      fps: this.outFps,
      lastError: this.lastError,
    };
  }
  get hasPainted() {
    return (this.video.getVideoPlaybackQuality?.()?.totalVideoFrames ?? this.appended) > 0;
  }

  /** Negotiated stream size (from CONFIG) used for the init segment's track header. */
  setDisplaySize(w: number, h: number) {
    if (w > 0 && h > 0) {
      this.displayW = w;
      this.displayH = h;
    }
  }

  setFit(mode: FitMode) {
    this.video.style.objectFit = mode === "stretch" ? "fill" : mode === "cover" ? "cover" : "contain";
  }
  setAdjust(flip: boolean, brightness: number, contrast: number) {
    this.opts = { flip, brightness, contrast };
    this.applyAdjust();
  }
  private applyAdjust() {
    const f = this.opts;
    this.video.style.transform = f.flip ? "scaleX(-1)" : "";
    this.video.style.filter =
      f.brightness !== 1 || f.contrast !== 1 ? `brightness(${f.brightness}) contrast(${f.contrast})` : "";
  }
  setClock() {
    /* MSE self-paces on the <video> element's own clock. */
  }
  setKeyframeRequester(fn: (() => void) | null) {
    this.requestKeyframe = fn;
  }

  submit(keyframe: boolean, nal: Uint8Array, ptsUs: bigint): void {
    if (this.closed) this.closed = false;
    this.totalRecv++;
    this.recvWin++;
    const now = performance.now();
    if (now - this.recvAt >= 1000) {
      // Scale by the real window: at low fps it stretches past 1s and a raw count lies.
      const winS = (now - this.recvAt) / 1000;
      this.recvFps = Math.round(this.recvWin / winS);
      this.recvWin = 0;
      this.recvAt = now;
      const q = this.video.getVideoPlaybackQuality?.();
      if (q) {
        this.outFps = Math.round((q.totalVideoFrames - this.lastRendered) / winS);
        this.lastRendered = q.totalVideoFrames;
      } else {
        this.outFps = this.recvFps; // no quality API: assume it keeps up
      }
    }

    const units = parseAnnexB(nal);
    if (!this.sps || !this.pps) {
      const ps = extractParamSets(units);
      if (ps.sps) this.sps = ps.sps;
      if (ps.pps) this.pps = ps.pps;
    }
    if (!this.started) {
      if (!keyframe || !this.sps || !this.pps) return; // wait for a keyframe carrying params
      this.codec = avcCodecString(nal) ?? "avc1.42E01F";
      this.start();
    }

    const pts = Number(ptsUs);
    if (this.firstPtsUs == null) this.firstPtsUs = pts;
    const sample = annexBToAvcc(units);
    if (sample.length === 0) return;
    const cur = { sample, ptsUs: pts, keyframe };
    // Hold one frame so its duration = (nextPTS − thisPTS): exact, gapless timing.
    if (this.prev) {
      const dur = Math.max(1, cur.ptsUs - this.prev.ptsUs);
      this.emit(this.prev, dur);
    }
    this.prev = cur;
  }

  private emit(frame: { sample: Uint8Array; ptsUs: number; keyframe: boolean }, duration: number): void {
    const base = frame.ptsUs - (this.firstPtsUs ?? frame.ptsUs);
    const seg = buildSegment({
      sample: frame.sample,
      baseMediaDecodeTime: base,
      duration,
      keyframe: frame.keyframe,
      sequenceNumber: this.seq++,
    });
    this.appended++;
    this.enqueue(seg);
  }

  private start(): void {
    this.started = true; // guard re-entry; actual SourceBuffer set up on sourceopen
    try {
      const ms = new MediaSource();
      this.ms = ms;
      this.video.src = URL.createObjectURL(ms);
      ms.addEventListener(
        "sourceopen",
        () => {
          try {
            const mime = `video/mp4; codecs="${this.codec}"`;
            if (!("MediaSource" in window) || !MediaSource.isTypeSupported(mime)) {
              this.fail(`unsupported codec ${mime}`);
              return;
            }
            const sb = ms.addSourceBuffer(mime);
            sb.mode = "segments";
            this.sb = sb;
            sb.addEventListener("updateend", () => {
              this.busy = false;
              this.flush();
            });
            sb.addEventListener("error", () => this.fail("SourceBuffer error"));
            // Prepend the init segment ahead of any queued media.
            this.queue.unshift(buildInit(this.sps!, this.pps!, this.displayW, this.displayH));
            this.flush();
          } catch (e) {
            this.fail(String((e as Error)?.message || e));
          }
        },
        { once: true },
      );
      this.video.play?.().catch(() => {
        /* muted autoplay is allowed; ignore rejection */
      });
    } catch (e) {
      this.fail(String((e as Error)?.message || e));
    }
  }

  private enqueue(buf: Uint8Array): void {
    this.queue.push(buf);
    this.flush();
  }

  private flush(): void {
    const sb = this.sb;
    if (this.closed || !sb || this.busy || sb.updating) return;
    const next = this.queue.shift();
    if (!next) {
      this.maybeTrim();
      return;
    }
    this.busy = true;
    try {
      sb.appendBuffer(next as BufferSource);
    } catch (e) {
      this.busy = false;
      if ((e as Error)?.name === "QuotaExceededError") {
        this.queue.unshift(next); // retry after freeing space
        this.trimHard();
      } else {
        this.fail(String((e as Error)?.message || e));
      }
    }
  }

  /** Bound memory + latency: drop old buffered data and chase the live edge. */
  private maybeTrim(): void {
    const v = this.video;
    const sb = this.sb;
    if (!sb || sb.updating) return;
    try {
      const b = v.buffered;
      if (!b.length) return;
      const end = b.end(b.length - 1);
      const start = b.start(0);
      if (v.currentTime < end - 0.6) v.currentTime = end - 0.1; // stay near live
      const cutoff = v.currentTime - 2;
      if (cutoff > start + 0.5) {
        this.busy = true; // remove is async; block appends until its updateend
        sb.remove(start, cutoff);
      }
    } catch {
      /* ignore */
    }
  }

  private trimHard(): void {
    const v = this.video;
    const sb = this.sb;
    if (!sb || sb.updating) return;
    try {
      const b = v.buffered;
      if (!b.length) return;
      const start = b.start(0);
      const cutoff = Math.max(start + 0.1, v.currentTime - 0.5);
      if (cutoff > start) {
        this.busy = true;
        sb.remove(start, cutoff);
      }
    } catch {
      /* ignore */
    }
  }

  private fail(msg: string): void {
    this.lastError = msg;
    this.onInfo?.(`MSE: ${msg}`);
    // A fresh IDR is what lets the SourceBuffer resync; without asking we would wait for
    // the host's scheduled keyframe.
    this.requestKeyframe?.();
  }

  close(): void {
    this.closed = true;
    try {
      if (this.ms && this.ms.readyState === "open") this.ms.endOfStream();
    } catch {
      /* ignore */
    }
    try {
      this.video.pause();
      this.video.removeAttribute("src");
      this.video.load();
    } catch {
      /* ignore */
    }
    this.sb = null;
    this.ms = null;
    this.queue = [];
    this.prev = null;
    this.started = false;
    this.busy = false;
    this.sps = undefined;
    this.pps = undefined;
    this.firstPtsUs = null;
    this.seq = 1;
    this.appended = 0;
    this.lastRendered = 0;
  }
}
