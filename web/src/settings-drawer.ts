import { loadSettings, saveSettings, type ClientSettings } from "./settings.ts";
import { setTheme, type Theme } from "./theme.ts";
import type { FitMode } from "./fit.ts";

/** parseInt with a NaN -> 0 guard, clamped to >= 0 (wall col/row are grid coords). */
function intOrZero(v: string): number {
  const n = parseInt(v, 10);
  return Number.isFinite(n) && n >= 0 ? n : 0;
}

/** parseFloat with a NaN -> fallback guard (brightness/contrast are CSS filter multipliers). */
function floatOr(v: string, fallback: number): number {
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : fallback;
}

/**
 * Side-drawer (bottom-sheet on narrow viewports) surfacing every
 * `ClientSettings` field. Seeded once from `loadSettings()` at construction;
 * every control change re-reads the whole control set, persists it, and
 * hands the result to the caller's `onChange` for live-apply.
 */
export class SettingsDrawer {
  private readonly app = document.getElementById("app")!;
  private readonly scrim = document.getElementById("scrim")!;
  private readonly drawer = document.getElementById("drawer")!;
  private readonly closeBtn = document.getElementById("drawer-close") as HTMLButtonElement;

  private readonly quality = document.getElementById("set-quality") as HTMLSelectElement;
  private readonly fps = document.getElementById("set-fps") as HTMLSelectElement;
  private readonly audio = document.getElementById("set-audio") as HTMLInputElement;
  private readonly fitSeg = document.getElementById("set-fit") as HTMLElement;
  private readonly flip = document.getElementById("set-flip") as HTMLInputElement;
  private readonly brightness = document.getElementById("set-brightness") as HTMLInputElement;
  private readonly contrast = document.getElementById("set-contrast") as HTMLInputElement;
  private readonly wallCol = document.getElementById("set-wall-col") as HTMLInputElement;
  private readonly wallRow = document.getElementById("set-wall-row") as HTMLInputElement;
  private readonly name = document.getElementById("set-name") as HTMLInputElement;
  private readonly themeSeg = document.getElementById("set-theme") as HTMLElement;

  private fit: FitMode = "contain";
  private theme: Theme = "dark";

  constructor(private readonly onChange: (s: ClientSettings) => void) {
    const s = loadSettings();
    this.quality.value = String(s.bitrateKbps);
    this.fps.value = String(s.fps);
    this.audio.checked = s.audio;
    this.flip.checked = s.flip;
    this.brightness.value = String(s.brightness);
    this.contrast.value = String(s.contrast);
    this.wallCol.value = String(s.wallCol);
    this.wallRow.value = String(s.wallRow);
    this.name.value = s.name;
    this.fit = s.fit;
    this.theme = s.theme;
    this.setSeg(this.fitSeg, this.fit);
    this.setSeg(this.themeSeg, this.theme);

    this.quality.addEventListener("change", () => this.commit());
    this.fps.addEventListener("change", () => this.commit());
    this.audio.addEventListener("change", () => this.commit());
    this.flip.addEventListener("change", () => this.commit());
    this.brightness.addEventListener("input", () => this.commit());
    this.contrast.addEventListener("input", () => this.commit());
    this.wallCol.addEventListener("input", () => this.commit());
    this.wallRow.addEventListener("input", () => this.commit());
    this.name.addEventListener("input", () => this.commit());

    this.bindSeg(this.fitSeg, (v) => {
      this.fit = v as FitMode;
      this.commit();
    });
    this.bindSeg(this.themeSeg, (v) => {
      this.theme = v as Theme;
      setTheme(this.theme);
      this.commit();
    });

    this.scrim.addEventListener("click", () => this.close());
    this.closeBtn.addEventListener("click", () => this.close());
    window.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && this.app.classList.contains("drawer-open")) this.close();
    });
  }

  private setSeg(el: HTMLElement, value: string): void {
    el.querySelectorAll<HTMLButtonElement>(".seg-btn").forEach((b) => {
      const active = b.dataset.value === value;
      b.classList.toggle("is-active", active);
      b.setAttribute("aria-pressed", String(active));
    });
  }

  private bindSeg(el: HTMLElement, onPick: (value: string) => void): void {
    el.querySelectorAll<HTMLButtonElement>(".seg-btn").forEach((b) => {
      b.addEventListener("click", () => {
        const value = b.dataset.value!;
        this.setSeg(el, value);
        onPick(value);
      });
    });
  }

  /** Reads every control, persists the merged settings, and notifies the caller. */
  private commit(): void {
    // loadSettings() carries fields the drawer doesn't expose (id) forward untouched.
    const prev = loadSettings();
    const s: ClientSettings = {
      ...prev,
      bitrateKbps: parseInt(this.quality.value, 10) || prev.bitrateKbps,
      fps: parseInt(this.fps.value, 10) || prev.fps,
      audio: this.audio.checked,
      fit: this.fit,
      flip: this.flip.checked,
      brightness: floatOr(this.brightness.value, 1),
      contrast: floatOr(this.contrast.value, 1),
      wallCol: intOrZero(this.wallCol.value),
      wallRow: intOrZero(this.wallRow.value),
      name: this.name.value,
      theme: this.theme,
    };
    saveSettings(s);
    this.onChange(s);
  }

  open(): void {
    this.scrim.hidden = false;
    this.drawer.hidden = false;
    this.app.classList.add("drawer-open");
  }

  close(): void {
    this.app.classList.remove("drawer-open");
  }
}
