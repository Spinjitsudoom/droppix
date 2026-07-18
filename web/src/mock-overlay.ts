import { contentBox, type FitMode } from "./fit.ts";

/**
 * Server-feedback only: yellow SRV marks from /debug/server-marks while connected.
 * No local preview / log wallpaper when disconnected.
 */
export class MockOverlay {
  private layer: HTMLElement;
  private logEl: HTMLElement;
  private backdrop: HTMLCanvasElement;
  private pollTimer = 0;
  private seenMarks = new Set<string>();
  private videoW = 1280;
  private videoH = 720;
  private fit: FitMode = "contain";
  private connected = false;

  constructor(
    _stage: HTMLElement,
    layer: HTMLElement,
    logEl: HTMLElement,
    backdrop: HTMLCanvasElement,
    private canvas: HTMLCanvasElement,
  ) {
    this.layer = layer;
    this.logEl = logEl;
    this.backdrop = backdrop;
    this.showIdle();
  }

  /** Idle / disconnected: black stage, no mock chrome. */
  showIdle() {
    this.connected = false;
    this.stopServerMarkPoll();
    this.backdrop.hidden = true;
    this.backdrop.classList.add("is-hidden");
    this.logEl.hidden = true;
    this.logEl.textContent = "";
    this.layer.hidden = true;
    this.layer.replaceChildren();
    this.seenMarks.clear();
    this.clearCanvas();
  }

  /** Connected: only server mark layer (no local preview). */
  showConnected() {
    this.connected = true;
    this.backdrop.hidden = true;
    this.backdrop.classList.add("is-hidden");
    this.logEl.hidden = true;
    this.layer.hidden = false;
  }

  setVideoSize(w: number, h: number) {
    this.videoW = w;
    this.videoH = h;
  }

  setFit(mode: FitMode) {
    this.fit = mode;
  }

  markVideoAlive() {
    /* no local backdrop to hide */
  }

  startServerMarkPoll() {
    this.stopServerMarkPoll();
    this.showConnected();
    void fetch("./debug/server-marks?clear=1").catch(() => {});
    this.pollTimer = window.setInterval(() => void this.pullMarks(), 150);
  }

  stopServerMarkPoll() {
    if (this.pollTimer) clearInterval(this.pollTimer);
    this.pollTimer = 0;
  }

  /** Kept for API compat; unused in idle-clean mode. */
  note(_msg: string) {}

  showClick(_x: number, _y: number, _label: string) {}

  private clearCanvas() {
    const c = this.canvas;
    const ctx = c.getContext("2d");
    if (!ctx) return;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, c.width || 1, c.height || 1);
    ctx.restore();
  }

  private async pullMarks() {
    if (!this.connected) return;
    try {
      const r = await fetch("./debug/server-marks", { cache: "no-store" });
      if (!r.ok) return;
      const j = (await r.json()) as {
        marks?: Array<{ t: number; kind: string; x?: number; y?: number }>;
      };
      for (const m of j.marks || []) {
        if (m.x == null || m.y == null) continue;
        if (
          m.kind === "touch-up" ||
          m.kind === "mouse-up" ||
          m.kind === "scroll" ||
          m.kind === "key"
        ) {
          continue;
        }
        const id = `${m.t}-${m.kind}-${m.x}-${m.y}`;
        if (this.seenMarks.has(id)) continue;
        this.seenMarks.add(id);
        const css = this.videoToCss(m.x, m.y);
        if (!css) continue;
        this.spawnMark(css.x, css.y, `SRV ${m.kind} ${m.x},${m.y}`);
      }
    } catch {
      /* ignore */
    }
  }

  private videoToCss(vx: number, vy: number): { x: number; y: number } | null {
    const r = this.canvas.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) return null;
    const box = contentBox(r.width, r.height, this.videoW, this.videoH, this.fit);
    return {
      x: box.x + (vx / Math.max(1, this.videoW - 1)) * box.w,
      y: box.y + (vy / Math.max(1, this.videoH - 1)) * box.h,
    };
  }

  private spawnMark(x: number, y: number, label: string) {
    const mark = document.createElement("div");
    mark.className = "click-mark click-mark-srv";
    mark.style.left = `${x}px`;
    mark.style.top = `${y}px`;
    mark.innerHTML = `<span class="click-ring"></span><span class="click-label">${label}</span>`;
    this.layer.appendChild(mark);
    window.setTimeout(() => mark.remove(), 1600);
  }

  dispose() {
    this.showIdle();
  }
}
