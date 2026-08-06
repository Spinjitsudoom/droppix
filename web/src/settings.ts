import type { FitMode } from "./fit.ts";
import type { Theme } from "./theme.ts";

export interface ClientSettings {
  name: string;
  id: string;
  fps: number;
  bitrateKbps: number;
  /** "auto" (canvas × devicePixelRatio) or a fixed "WIDTHxHEIGHT" the host renders. */
  resolution: string;
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
    fps: 30,
    bitrateKbps: 8000,
    resolution: "1280x720",
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
 * Resolve the HELLO width/height from the `resolution` setting. A fixed
 * "WIDTHxHEIGHT" wins; "auto" (or anything unparseable) falls back to `auto`,
 * which callers derive from the canvas × devicePixelRatio. Fixed dimensions are
 * rounded down to even — H.264 requires even width/height, so an odd request
 * would otherwise force encoder padding / a non-standard size.
 */
export function resolveResolution(
  setting: string,
  auto: { w: number; h: number },
): { w: number; h: number } {
  const m = /^(\d+)x(\d+)$/.exec(setting);
  if (!m) return auto;
  const w = parseInt(m[1]!, 10);
  const h = parseInt(m[2]!, 10);
  if (!Number.isFinite(w) || !Number.isFinite(h) || w < 2 || h < 2) return auto;
  return { w: w - (w % 2), h: h - (h % 2) };
}
