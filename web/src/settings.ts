import type { FitMode } from "./fit.ts";
import type { Theme } from "./theme.ts";

export interface ClientSettings {
  name: string;
  id: string;
  fps: number;
  bitrateKbps: number;
  /** "auto" (canvas × devicePixelRatio) or a fixed "WIDTHxHEIGHT" the host renders. */
  resolution: string;
  /** Video render path: "canvas" (WebCodecs→Canvas 2D) or "mse" (native <video> via MSE). */
  renderer: string;
  audio: boolean;
  fit: FitMode;
  flip: boolean;
  brightness: number;
  contrast: number;
  wallCol: number;
  wallRow: number;
  theme: Theme;
}

const KEY = "droppix.web.settings.v1";

function randomId(): string {
  const a = new Uint8Array(8);
  crypto.getRandomValues(a);
  return Array.from(a, (b) => b.toString(16).padStart(2, "0")).join("");
}

export function loadSettings(): ClientSettings {
  const defaults: ClientSettings = {
    name: "Web PWA",
    id: randomId(),
    // 60 by default: the host's virtual monitor also defaults to 60 Hz, and the client's
    // request drives both the encoder AND (now) the monitor's mode. Weak links degrade
    // gracefully via the host's send-backlog pacing rather than by capping everyone at 30.
    fps: 60,
    bitrateKbps: 8000,
    resolution: "720",
    renderer: "canvas",
    audio: true,
    fit: "contain",
    flip: false,
    brightness: 1,
    contrast: 1,
    wallCol: 0,
    wallRow: 0,
    theme: "dark",
  };
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) {
      saveSettings(defaults);
      return defaults;
    }
    return { ...defaults, ...JSON.parse(raw) };
  } catch {
    return defaults;
  }
}

export function saveSettings(s: ClientSettings): void {
  localStorage.setItem(KEY, JSON.stringify(s));
}

/**
 * Resolve the HELLO width/height from the `resolution` setting.
 *
 * Presets are a TARGET HEIGHT, not a fixed WxH: the width is derived from the device's
 * own aspect ratio. A fixed preset (e.g. 1280x720, 16:9) on a 2340x1080 phone (19.5:9)
 * makes the host render the wrong SHAPE, which then has to be letterboxed or stretched to
 * fit the screen — the picture never looks right. Matching the device's aspect means the
 * streamed image fills the screen exactly, at any quality level.
 *
 * "auto" (or anything unparseable) uses the canvas-derived size. We never upscale past
 * the device: asking for more pixels than the screen has only costs bandwidth.
 *
 * Dimensions are rounded down to even — H.264 requires even width/height.
 */
export function resolveResolution(
  setting: string,
  auto: { w: number; h: number },
): { w: number; h: number } {
  if (auto.w < 2 || auto.h < 2) return auto;
  if (setting === "auto") return auto;

  // Accept a bare height ("720") and, for settings saved by older builds, a legacy
  // "WIDTHxHEIGHT" whose height we reuse as the target.
  const legacy = /^(\d+)x(\d+)$/.exec(setting);
  const targetH = legacy ? parseInt(legacy[2]!, 10) : parseInt(setting, 10);
  if (!Number.isFinite(targetH) || targetH < 2) return auto;
  if (targetH >= auto.h) return auto;   // never upscale beyond the device

  const aspect = auto.w / auto.h;
  const h = targetH;
  const w = Math.round(h * aspect);
  return { w: Math.max(2, w - (w % 2)), h: Math.max(2, h - (h % 2)) };
}

/**
 * Merge settings the HOST was holding over the local copy.
 *
 * The host stores this blob on the client's behalf because the browser will not reliably:
 * localStorage is scoped to the exact origin (droppix's session port moves between runs) and
 * browsers drop it for origins whose certificate the user had to click through.
 *
 * `id` is deliberately NOT adopted. It is this browser's identity — the key the host's
 * approved-device store recognises — so taking one from the blob would make this client
 * masquerade as whichever device saved last, inheriting its approval.
 *
 * Anything malformed, empty, or non-object leaves the local settings untouched: a bad blob
 * must never cost the user their configuration or block the stream.
 */
export function mergeHostSettings(local: ClientSettings, json: string): ClientSettings {
  let parsed: unknown;
  try {
    parsed = JSON.parse(json);
  } catch {
    return local;
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) return local;
  const { id: _hostId, ...rest } = parsed as Record<string, unknown>;
  if (Object.keys(rest).length === 0) return local;
  return { ...local, ...(rest as Partial<ClientSettings>) };
}
