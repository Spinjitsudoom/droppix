import type { FitMode } from "./fit.ts";

export interface ClientSettings {
  name: string;
  id: string;
  fps: number;
  bitrateKbps: number;
  audio: boolean;
  fit: FitMode;
  flip: boolean;
  brightness: number;
  contrast: number;
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
    audio: true,
    fit: "contain",
    flip: false,
    brightness: 1,
    contrast: 1,
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
