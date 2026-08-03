export interface SessionControlsOptions {
  onDisconnect: () => void;
  onFullscreen: () => void;
  onMute: () => void;
  onHud: () => void;
  onSettings: () => void;
  onFit: () => void;
}

/** How long the bar stays visible after the last pointer activity. */
const IDLE_MS = 2500;

/**
 * In-session control bar: six buttons already rendered into `#controls` by
 * index.html (data-act="fit|mute|hud|settings|fullscreen|disconnect"). This
 * class only binds callbacks and owns the idle auto-hide/re-show behavior.
 */
export class SessionControls {
  private readonly app: HTMLElement;
  private readonly stage: HTMLElement;
  private readonly controls: HTMLElement;
  private hideTimer: number | null = null;

  constructor(opts: SessionControlsOptions) {
    this.app = document.getElementById("app")!;
    this.stage = document.getElementById("stage")!;
    this.controls = document.getElementById("controls")!;

    const bind = (act: string, fn: () => void) => {
      this.controls
        .querySelector<HTMLButtonElement>(`[data-act="${act}"]`)
        ?.addEventListener("click", fn);
    };
    bind("fit", opts.onFit);
    bind("mute", opts.onMute);
    bind("hud", opts.onHud);
    bind("settings", opts.onSettings);
    bind("fullscreen", opts.onFullscreen);
    bind("disconnect", opts.onDisconnect);

    // Any pointer activity over the video stage re-arms the visible window.
    this.stage.addEventListener("pointermove", () => this.show());
    this.stage.addEventListener("pointerdown", () => this.show());
  }

  /** Clears `.hidden` and (re)arms the auto-hide timer. */
  show(): void {
    this.controls.classList.remove("hidden");
    if (this.hideTimer !== null) window.clearTimeout(this.hideTimer);
    this.hideTimer = window.setTimeout(() => {
      // Only the session view auto-hides; a stray timer firing after the
      // user disconnected must not touch the bar in the connect view.
      if (this.app.dataset.view === "session") this.controls.classList.add("hidden");
    }, IDLE_MS);
  }
}
