import { MsgType, encodeTouch, encodeScroll, encodeMouseButton, encodeKey, type TouchContact } from "./protocol.ts";
import { contentBox, normalizePointer, type FitMode } from "./fit.ts";
import { codeToEvdev } from "./keymap.ts";

export type SendFn = (type: MsgType, body: Uint8Array) => void;

export class InputBinder {
  private pointers = new Map<number, TouchContact>();
  private videoW = 1280;
  private videoH = 720;
  private fit: FitMode = "contain";
  private lastNorm = { x: 32767, y: 32767 };

  constructor(
    private canvas: HTMLCanvasElement,
    private send: SendFn,
  ) {
    canvas.addEventListener("pointerdown", (e) => this.onPointerDown(e));
    canvas.addEventListener("pointermove", (e) => this.onPointerMove(e));
    canvas.addEventListener("pointerup", (e) => this.onPointerUp(e));
    canvas.addEventListener("pointercancel", (e) => this.onPointerUp(e));
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    canvas.addEventListener("wheel", (e) => this.onWheel(e), { passive: false });
    window.addEventListener("keydown", (e) => this.onKey(e, 1));
    window.addEventListener("keyup", (e) => this.onKey(e, 0));
  }

  setVideoSize(w: number, h: number) {
    this.videoW = w;
    this.videoH = h;
  }

  setFit(mode: FitMode) {
    this.fit = mode;
  }

  private localXY(e: PointerEvent): { x: number; y: number } {
    const r = this.canvas.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }

  private box() {
    const r = this.canvas.getBoundingClientRect();
    return contentBox(r.width, r.height, this.videoW, this.videoH, this.fit);
  }

  private normFromEvent(e: PointerEvent, outsideOk: boolean) {
    const { x, y } = this.localXY(e);
    return normalizePointer(x, y, this.box(), outsideOk);
  }

  private flushTouch() {
    const contacts = [...this.pointers.values()];
    this.send(MsgType.Touch, encodeTouch(contacts));
  }

  private onPointerDown(e: PointerEvent) {
    this.canvas.setPointerCapture(e.pointerId);
    const n = this.normFromEvent(e, this.fit !== "contain");
    if (!n) return;
    this.lastNorm = n;
    if (e.button === 2) {
      this.send(MsgType.MouseButton, encodeMouseButton(1, 1, n.x, n.y));
      return;
    }
    if (e.button === 1) {
      this.send(MsgType.MouseButton, encodeMouseButton(2, 1, n.x, n.y));
      return;
    }
    if (e.button !== 0) return;
    const pressure = Math.round(Math.min(1, Math.max(0, e.pressure || 1)) * 1023);
    this.pointers.set(e.pointerId, { id: e.pointerId & 0xff, x: n.x, y: n.y, pressure });
    this.flushTouch();
  }

  private onPointerMove(e: PointerEvent) {
    if (!this.pointers.has(e.pointerId)) return;
    const n = this.normFromEvent(e, true);
    if (!n) return;
    this.lastNorm = n;
    const pressure = Math.round(Math.min(1, Math.max(0, e.pressure || 1)) * 1023);
    this.pointers.set(e.pointerId, { id: e.pointerId & 0xff, x: n.x, y: n.y, pressure });
    this.flushTouch();
  }

  private onPointerUp(e: PointerEvent) {
    const n = this.normFromEvent(e, true) ?? this.lastNorm;
    if (e.button === 2) {
      this.send(MsgType.MouseButton, encodeMouseButton(1, 0, n.x, n.y));
    } else if (e.button === 1) {
      this.send(MsgType.MouseButton, encodeMouseButton(2, 0, n.x, n.y));
    }
    if (this.pointers.delete(e.pointerId)) this.flushTouch();
  }

  private onWheel(e: WheelEvent) {
    e.preventDefault();
    const r = this.canvas.getBoundingClientRect();
    const n =
      normalizePointer(e.clientX - r.left, e.clientY - r.top, this.box(), true) ?? this.lastNorm;
    const dy = Math.round(-e.deltaY / 120) || (e.deltaY < 0 ? 1 : e.deltaY > 0 ? -1 : 0);
    const dx = Math.round(e.deltaX / 120) || (e.deltaX > 0 ? 1 : e.deltaX < 0 ? -1 : 0);
    if (dx === 0 && dy === 0) return;
    this.send(MsgType.Scroll, encodeScroll(dx, dy, n.x, n.y));
  }

  private onKey(e: KeyboardEvent, action: number) {
    if (e.key === "F" || e.key === "f") return; // fullscreen shortcut handled elsewhere
    const code = codeToEvdev(e.code);
    if (!code) return;
    e.preventDefault();
    this.send(MsgType.Key, encodeKey(code, action));
  }
}
