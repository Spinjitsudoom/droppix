# Web PWA Client Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the web PWA client's UI shell into a two-state app matching the redesigned host — a PIN-entry connect screen, an auto-hiding in-session control bar, and a settings drawer surfacing the settings hidden in localStorage today — with a shared dark/light spacedesk theme.

**Architecture:** UI-shell + settings-surface + theming change only. `web/public/index.html` and `styles.css` are rewritten; `main.ts` becomes a slim orchestrator wiring four new focused modules (`pin.ts`, `theme.ts`, `connect-view.ts`, `session-controls.ts`, `settings-drawer.ts`). The transport/decoder/audio/input/fit/fullscreen/mock modules and the wire protocol are untouched. Vanilla TypeScript + esbuild, no framework.

**Tech Stack:** TypeScript, esbuild bundler, Node `--test` runner, PWA service worker, CSS custom-property theming.

## Global Constraints

- **No wire/protocol change** — `web/src/protocol.ts` and `transport.ts` HELLO/framing stay as-is (HELLO v6). **No** changes to `decoder.ts`, `audio.ts`, `audio-worklet.ts`, `input.ts`, `fit.ts`, `fullscreen.ts`, `keymap.ts`, `mock-overlay.ts`.
- **Preserve these DOM element IDs** (the untouched modules receive them from `main.ts` via `getElementById`): `video` (canvas), `stage`, `mock-backdrop`, `click-layer`, `mock-log`, `hud`. Renaming any of these breaks decode/mock/input.
- **PIN = local match** against `config.json`'s `pairingCode` (kept in memory, never rendered). No server-side auth.
- **Theme:** dark default; both themes from one token set; teal accent `#14b8a6`; persisted in `settings.theme`.
- **Build/test off-mount** (the CIFS source mount can't host `node_modules`): copy `src public tests build.mjs package*.json tsconfig.json` to `~/droppix-web-build`, `npm ci`, then `npm test` (Node `--test`) and `npm run build` (esbuild). There is **no standalone `tsc`** — esbuild is the build/typecheck. Node ≥ 22 (uses `--experimental-strip-types`).
- **`web/dist` is a committed artifact** (Flatpak/AppImage consume it) — rebuild it off-mount and commit it whenever `web/` sources change (Task 6). **Bump `sw.js` shell cache** `droppix-shell-v4` → `v5` when `index.html`/`styles.css` change.
- Keep existing `web/tests/*.test.ts` green.

**Off-mount build recipe** (used by every task's verification):
```bash
rm -rf ~/droppix-web-build && mkdir -p ~/droppix-web-build && \
cp -r "/var/mnt/nas/Projects/Spacedesk for linux/web/"{src,public,tests,build.mjs,package.json,package-lock.json,tsconfig.json} ~/droppix-web-build/ && \
cd ~/droppix-web-build && npm ci --no-audit --no-fund && npm test && npm run build
```

---

### Task 1: Pure PIN helper (`pin.ts`)

**Files:**
- Create: `web/src/pin.ts`
- Test: `web/tests/pin.test.ts`

**Interfaces:**
- Produces: `normalizePin(raw: string): string` (digits only, ≤6); `pinComplete(code: string): boolean` (exactly 6 digits); `pinMatches(entered: string, served: string): boolean` (equal after normalize).

- [ ] **Step 1: Write the failing test** — `web/tests/pin.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { normalizePin, pinComplete, pinMatches } from "../src/pin.ts";

test("normalizePin strips non-digits and caps at 6", () => {
  assert.equal(normalizePin("04a2 913x99"), "042913");
  assert.equal(normalizePin(""), "");
});
test("pinComplete is true only at 6 digits", () => {
  assert.equal(pinComplete("042913"), true);
  assert.equal(pinComplete("0429"), false);
  assert.equal(pinComplete("0429134"), false); // 7 raw, but caller passes normalized
});
test("pinMatches compares normalized", () => {
  assert.equal(pinMatches("042 913", "042913"), true);
  assert.equal(pinMatches("042914", "042913"), false);
});
```

- [ ] **Step 2: Run it — FAIL** (module missing): off-mount recipe, expect `pin.test.ts` failing to import.

- [ ] **Step 3: Implement `web/src/pin.ts`:**
```ts
/** Pairing-PIN helpers. The web client validates the typed code locally against
 *  the pairingCode the host serves in config.json — a "right PC" check, not auth. */
export function normalizePin(raw: string): string {
  return raw.replace(/\D/g, "").slice(0, 6);
}
export function pinComplete(code: string): boolean {
  return /^\d{6}$/.test(code);
}
export function pinMatches(entered: string, served: string): boolean {
  return normalizePin(entered) === normalizePin(served);
}
```

- [ ] **Step 4: Run tests — PASS** (off-mount recipe; all `web/tests` green incl. the 3 new).

- [ ] **Step 5: Commit**
```bash
git add web/src/pin.ts web/tests/pin.test.ts
git commit -m "feat(web): pure pairing-PIN helper (normalize/complete/match)"
```

---

### Task 2: `theme.ts` + `settings.theme`

**Files:**
- Modify: `web/src/settings.ts` (add `theme` field + default)
- Create: `web/src/theme.ts`
- Test: `web/tests/theme.test.ts`

**Interfaces:**
- Consumes: `loadSettings`/`saveSettings` + `ClientSettings` (`settings.ts`).
- Produces: `type Theme = "dark" | "light"`; `nextTheme(t: Theme): Theme`; `applyTheme(t: Theme): void` (sets `document.documentElement` `data-theme`); `initTheme(): Theme` (reads `loadSettings().theme`, applies, returns it); `setTheme(t: Theme): void` (applies + persists via `saveSettings`).

- [ ] **Step 1: Write the failing test** — `web/tests/theme.test.ts` (pure part only; DOM parts aren't unit-tested):
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { nextTheme } from "../src/theme.ts";
test("nextTheme flips", () => {
  assert.equal(nextTheme("dark"), "light");
  assert.equal(nextTheme("light"), "dark");
});
```

- [ ] **Step 2: Run it — FAIL** (module missing).

- [ ] **Step 3a: Add `theme` to `ClientSettings`** — in `web/src/settings.ts`, add `theme: Theme;` to the `ClientSettings` interface (import the type from `./theme.ts`) and `theme: "dark",` to the `defaults` object in `loadSettings()`. (The existing spread `{ ...defaults, ...JSON.parse(raw) }` gives old installs `theme: "dark"` automatically.)

- [ ] **Step 3b: Implement `web/src/theme.ts`:**
```ts
import { loadSettings, saveSettings } from "./settings.ts";
export type Theme = "dark" | "light";
export function nextTheme(t: Theme): Theme { return t === "dark" ? "light" : "dark"; }
export function applyTheme(t: Theme): void {
  document.documentElement.setAttribute("data-theme", t);
}
export function initTheme(): Theme {
  const t = loadSettings().theme;
  applyTheme(t);
  return t;
}
export function setTheme(t: Theme): void {
  applyTheme(t);
  const s = loadSettings(); s.theme = t; saveSettings(s);
}
```
(To avoid an import cycle: `settings.ts` imports only the `Theme` **type** from `theme.ts` — `import type { Theme } from "./theme.ts"` — a type-only import esbuild erases, so there's no runtime cycle.)

- [ ] **Step 4: Run tests — PASS** (off-mount recipe green; esbuild build clean).

- [ ] **Step 5: Commit**
```bash
git add web/src/theme.ts web/src/settings.ts web/tests/theme.test.ts
git commit -m "feat(web): theme module + persisted settings.theme"
```

---

### Task 3: New shell — markup, dual-theme styles, connect flow

Rewrite the page shell (connect + session + drawer containers), restyle to the spacedesk dual-theme tokens, and re-point `main.ts` at the new connect flow (PIN entry → local match → `transport.connect`). App builds and connects under the new shell; the session control bar and drawer land in Tasks 4–5.

**Files:**
- Rewrite: `web/public/index.html`, `web/public/styles.css`
- Create: `web/src/connect-view.ts`
- Modify: `web/src/main.ts` (orchestration)

**Interfaces:**
- Consumes: `pin.ts` (Task 1), `theme.ts` (Task 2), and the unchanged `Transport`/`VideoPipeline`/`AudioPlayer`/`InputBinder`/`MockOverlay`.
- Produces: DOM contract — app root `#app` with `data-view="connect"|"session"`; connect elements `#pin` (6 `<input>`s), `#btn-connect`, `#c-status`; header `#btn-theme`, `#btn-install`, `#mock-badge`; session elements `#stage`, `#video`, `#mock-backdrop`, `#click-layer`, `#mock-log`, `#hud`, `#status-pill`, and an empty `#controls` + `#drawer`/`#scrim` containers (populated in Tasks 4–5). `ConnectView` class in `connect-view.ts`: `new ConnectView(onConnect: (code: string) => void)`, methods `showError(msg: string)`, `reset()`.

- [ ] **Step 1: Rewrite `web/public/index.html`**

Structure (keep the flagged IDs verbatim — `video`, `stage`, `mock-backdrop`, `click-layer`, `mock-log`, `hud`):
```html
<div id="app" data-view="connect">
  <header id="topbar">
    <div class="brand">…logo… droppi<span>x</span></div>
    <span id="mock-badge" class="mock-badge" hidden>MOCK</span>
    <button id="btn-install" type="button" hidden>Install</button>
    <button id="btn-theme" type="button" aria-label="Toggle theme">◐</button>
  </header>

  <!-- CONNECT VIEW -->
  <section id="connect">
    <div class="card">
      <h1>Enter your PC's code</h1>
      <p class="lede">Open droppix on your computer and type the 6-digit code it shows.</p>
      <div id="pin" class="code">
        <input inputmode="numeric" maxlength="1" aria-label="Pairing digit 1" />
        …6 total…
      </div>
      <button id="btn-connect" class="btn-primary" type="button" disabled>Connect</button>
      <div id="c-status" class="status">Type the code shown on your PC</div>
    </div>
  </section>

  <!-- SESSION VIEW -->
  <main id="stage">
    <canvas id="mock-backdrop" hidden aria-hidden="true"></canvas>
    <canvas id="video" tabindex="0"></canvas>
    <div id="click-layer" hidden aria-hidden="true"></div>
    <pre id="mock-log" hidden></pre>
    <div id="hud" hidden></div>
    <div id="status-pill" hidden></div>
    <div id="controls" class="controls"></div>   <!-- filled in Task 4 -->
  </main>

  <div id="scrim" hidden></div>                    <!-- Task 5 -->
  <aside id="drawer" hidden></aside>               <!-- Task 5 -->
</div>
<script type="module" src="./main.js"></script>
```
CSS drives visibility: `#app[data-view="connect"] #stage { display:none }` and `#app[data-view="session"] #connect, #app[data-view="session"] #topbar { display:none }`.

- [ ] **Step 2: Rewrite `web/public/styles.css`** with the shared spacedesk token set:
```css
:root {
  --bg:#14181d; --surface:#1b1f24; --panel:#22272e; --panel-2:#262c34;
  --border:#2e343d; --border-strong:#3a424e; --text:#e6e9ef; --muted:#8a93a3;
  --accent:#14b8a6; --accent-2:#2dd4bf; --accent-text:#2dd4bf; --accent-ink:#06231f;
  --good:#22c55e; --warn:#f59e0b; --bad:#ef4444;
}
:root[data-theme="light"] {
  --bg:#eaeef2; --surface:#fff; --panel:#fff; --panel-2:#f4f6f9;
  --border:#dde3e9; --border-strong:#c6cfd8; --text:#131820; --muted:#5b6674;
  --accent:#14b8a6; --accent-2:#0f9e8e; --accent-text:#0c8579; --accent-ink:#fff;
  --good:#16a34a; --warn:#d97706; --bad:#dc2626;
}
```
Port the connect card, 6-box `.code` entry (focus ring, `.filled`, `.err` shake), `.btn-primary`, `#topbar`, and the existing `#stage`/`#video`/`#mock-backdrop`/`#hud`/`#click-layer`/`#mock-log` rules (copy their positioning from the current `styles.css` verbatim — the mock/decoder rely on `#mock-backdrop` z-index above `#video`). Add `#status-pill`. Style everything through the tokens (no hard-coded colors in component rules). Responsive: mobile-first, cards `max-width` on desktop.

- [ ] **Step 3: Implement `web/src/connect-view.ts`:**
```ts
import { normalizePin, pinComplete } from "./pin.ts";
export class ConnectView {
  private inputs: HTMLInputElement[];
  private btn: HTMLButtonElement;
  private wrap: HTMLElement;
  private statusEl: HTMLElement;
  constructor(private onConnect: (code: string) => void) {
    this.wrap = document.getElementById("pin")!;
    this.inputs = [...this.wrap.querySelectorAll("input")] as HTMLInputElement[];
    this.btn = document.getElementById("btn-connect") as HTMLButtonElement;
    this.statusEl = document.getElementById("c-status")!;
    this.inputs.forEach((inp, i) => {
      inp.addEventListener("input", () => {
        inp.value = normalizePin(inp.value).slice(0, 1);
        inp.classList.toggle("filled", !!inp.value);
        this.wrap.classList.remove("err");
        if (inp.value && i < this.inputs.length - 1) this.inputs[i + 1]!.focus();
        this.sync();
      });
      inp.addEventListener("keydown", (e) => {
        if (e.key === "Backspace" && !inp.value && i > 0) this.inputs[i - 1]!.focus();
      });
    });
    this.btn.addEventListener("click", () => { if (!this.btn.disabled) this.onConnect(this.value()); });
    this.sync();
  }
  private value(): string { return normalizePin(this.inputs.map((i) => i.value).join("")); }
  private sync(): void { this.btn.disabled = !pinComplete(this.value()); }
  showError(msg: string): void { this.wrap.classList.add("err"); this.statusEl.textContent = msg; }
  reset(): void { this.inputs.forEach((i) => { i.value = ""; i.classList.remove("filled"); }); this.wrap.classList.remove("err"); this.sync(); }
  focus(): void { this.inputs[0]?.focus(); }
}
```

- [ ] **Step 4: Rewire `web/src/main.ts`** — keep ALL transport/decoder/audio/input construction and the `wireTransport()` handlers (`onConfig`/`onVideo`/`onAudio`/`onOverlay`/`onClose`) exactly as today. Change only the shell wiring:
  - Remove the old `pin-ok`/`fit-mode`/`mute`/`wall-col`/`wall-row` element lookups and their handlers (those controls are gone; the drawer re-adds them in Task 5). Keep `settings` load and the `video`/`stage`/`mock-*`/`hud` lookups (unchanged IDs).
  - `import { initTheme, setTheme, nextTheme } from "./theme.ts";` and `import { ConnectView } from "./connect-view.ts";`.
  - At startup: `let theme = initTheme();` and wire `#btn-theme` → `{ theme = nextTheme(theme); setTheme(theme); }`.
  - Keep a module-level `let pairingCode = "------";` set in `loadConfig()` from `j.pairingCode` (do NOT render it).
  - `const connectView = new ConnectView((code) => { void tryConnect(code); });` where:
```ts
async function tryConnect(code: string) {
  const { pinMatches } = await import("./pin.ts");   // or top-level import
  if (!isMock && !pinMatches(code, pairingCode)) {
    connectView.showError("That code doesn't match your PC — check the screen");
    return;
  }
  app.dataset.view = "session";      // #app
  await connect();                   // the EXISTING connect() — unchanged transport.connect(...) call
}
```
  (Prefer a top-level `import { pinMatches } from "./pin.ts";`.) `connect()` keeps its current body: audio unlock, `wireTransport()`, compute `w/h`, `transport.connect({ width,height,density,name,id,fps,audioWanted,bitrateKbps,wallCol,wallRow })` from `settings`. On `onClose`/`disconnect()`, set `app.dataset.view = "connect"` and `connectView.reset()`.
  - **Mock mode** (`isMock` in `loadConfig`): instead of the old `pinOk.checked = true`, call `app.dataset.view = "session"` + auto-connect after the existing 300 ms delay (no PIN on a mock host). Keep the `MOCK` badge + start-muted behavior.
  - Keep `beforeinstallprompt` → `#btn-install` and the service-worker registration.

- [ ] **Step 5: Build + tests + manual-smoke** — off-mount recipe: `npm test` green, `npm run build` prints `built web/dist`. Confirm the bundle carries the new shell: `grep -c "btn-connect" ~/droppix-web-build/dist/index.html` ≥ 1. (Manual, user: PIN entry enables Connect at 6 digits; wrong code errors; right code connects; theme toggles.)

- [ ] **Step 6: Commit**
```bash
git add web/public/index.html web/public/styles.css web/src/connect-view.ts web/src/main.ts
git commit -m "feat(web): PIN-entry connect screen + dual-theme spacedesk shell"
```

---

### Task 4: In-session control bar (`session-controls.ts`)

**Files:**
- Create: `web/src/session-controls.ts`
- Modify: `web/public/index.html` (fill `#controls`), `web/public/styles.css` (control-bar styles), `web/src/main.ts` (wire it)

**Interfaces:**
- Consumes: callbacks from `main.ts`.
- Produces: `class SessionControls { constructor(opts: { onDisconnect(): void; onFullscreen(): void; onMute(): void; onHud(): void; onSettings(): void; onFit(): void }); show(): void; }` — renders buttons into `#controls`, runs a ~2500 ms idle auto-hide (adds `.hidden`), and re-shows on `pointermove`/`pointerdown` on `#stage`.

- [ ] **Step 1: Add the control buttons to `#controls` in `index.html`** — six `<button class="cbtn">` (Fit, Mute, HUD, Settings [accent], Fullscreen, Disconnect) with inline SVG icons + `aria-label`s, plus a `.hinttap` "Tap to show controls" element after `#controls`.

- [ ] **Step 2: Style the control bar in `styles.css`** — `.controls` (fixed bottom-center, `--surface` bg, blur, `--shadow`, `transition: opacity/transform`), `.controls.hidden` (opacity 0 + translateY + `pointer-events:none`), `.cbtn` (42px, hover `--panel-2`, `.accent` = teal, `.warn:hover` = `--bad`), `.hinttap` (shown when controls hidden).

- [ ] **Step 3: Implement `session-controls.ts`** — query the buttons in `#controls`, bind each to the matching `opts` callback; `show()` clears `.hidden` and (re)arms a `setTimeout` that adds `.hidden`; attach `pointermove`/`pointerdown` on `#stage` to `show()`. Guard the timer so it only auto-hides while `#app[data-view="session"]`.

- [ ] **Step 4: Wire in `main.ts`** — construct `SessionControls` with: `onDisconnect: disconnect`, `onFullscreen: () => toggleFullscreen(stage)`, `onMute: () => { const s = loadSettings(); s.audio = !s.audio; saveSettings(s); audio.setMuted(!s.audio); }`, `onHud: () => { showHud = !showHud; hud.hidden = !showHud; }`, `onSettings: () => openDrawer()` (a stub `openDrawer` that Task 5 implements — for now `() => {}`), `onFit: () => cycleFit()` (cycle contain→cover→stretch, `saveSettings`, `video.setFit`/`input.setFit`/`mock.setFit`). Call `controls.show()` when entering the session view (in `tryConnect`).

- [ ] **Step 5: Build** — off-mount `npm run build` clean + `npm test` green. (Manual: controls auto-hide after ~2.5 s, return on move/tap; mute/fit/HUD/fullscreen/disconnect work.)

- [ ] **Step 6: Commit**
```bash
git add web/src/session-controls.ts web/public/index.html web/public/styles.css web/src/main.ts
git commit -m "feat(web): auto-hiding in-session control bar"
```

---

### Task 5: Settings drawer (`settings-drawer.ts`)

**Files:**
- Create: `web/src/settings-drawer.ts`
- Modify: `web/public/index.html` (fill `#drawer`), `web/public/styles.css` (drawer + fields), `web/src/main.ts` (wire open + live-apply)

**Interfaces:**
- Consumes: `loadSettings`/`saveSettings`, `setTheme`/`nextTheme` (theme.ts), and live-apply callbacks.
- Produces: `class SettingsDrawer { constructor(onChange: (s: ClientSettings) => void); open(): void; close(): void; }` — binds every field to `ClientSettings`, persists on change (`saveSettings`), and calls `onChange(s)` so `main.ts` can apply live setters.

- [ ] **Step 1: Add the drawer markup to `index.html`** — `#scrim` + `#drawer` with grouped fields: **Stream** (Quality `<select>` 4000/8000/16000, FPS `<select>` 30/60, Audio toggle), **Display** (Fit segmented, Flip toggle, Brightness `range -100..100`, Contrast `range 0..200`), **Wall** (col/row `number`), **Device** (name `text`), **Appearance** (Theme segmented dark/light). Give each control a stable `id` (`set-quality`, `set-fps`, `set-audio`, `set-fit`, `set-flip`, `set-brightness`, `set-contrast`, `set-wall-col`, `set-wall-row`, `set-name`, `set-theme`) + a close button `#drawer-close`.

- [ ] **Step 2: Style the drawer** — `#drawer` slide-in from right (`transform: translateX(100%)` → `#app.drawer-open #drawer { transform:none }`), `#scrim` fade; `@media (max-width:560px)` switch to a bottom sheet (`translateY(100%)` from the bottom, rounded top). Field rows, `.seg`, `.tgl`, range styling via tokens.

- [ ] **Step 3: Implement `settings-drawer.ts`** — on construct, seed each control from `loadSettings()`; on any change, read all controls → build an updated `ClientSettings`, `saveSettings(s)`, call `onChange(s)`, and for the theme control call `setTheme`. `open()` adds `drawer-open` to `#app` + unhides `#scrim`/`#drawer`; `close()` reverses; wire `#scrim` click + `#drawer-close` + `Escape` to `close()`.

- [ ] **Step 4: Wire in `main.ts`** — `const drawer = new SettingsDrawer((s) => { settings = s; video.setAdjust(s.flip, s.brightness, s.contrast); video.setFit(s.fit); input?.setFit(s.fit); mock.setFit(s.fit); audio.setMuted(!s.audio); });`. Replace the Task-4 `openDrawer` stub so `onSettings` and `#btn-theme`’s neighbor gear open the drawer; also open it from the connect header (add a gear `#btn-settings` to `#topbar`, or reuse the drawer from the control bar only — per the mockup, reachable from both). Quality/fps/name/wall changes take effect on next connect (no live setter needed — they're read in `connect()` from `settings`).

- [ ] **Step 5: Build + tests** — off-mount `npm test` green + `npm run build` clean. (Manual: drawer surfaces all settings; brightness/contrast/fit/flip/mute apply live; theme persists across reload; narrow window → bottom sheet.)

- [ ] **Step 6: Commit**
```bash
git add web/src/settings-drawer.ts web/public/index.html web/public/styles.css web/src/main.ts
git commit -m "feat(web): settings drawer surfacing all client settings"
```

---

### Task 6: PWA cache bump, dist rebuild, docs

**Files:**
- Modify: `web/public/sw.js` (cache version), `docs/STATUS.md`, `docs/superpowers/specs/2026-08-03-web-client-redesign-design.md` (Status header)
- Rebuild + commit: `web/dist/**`

- [ ] **Step 1: Bump the service-worker shell cache** — in `web/public/sw.js` change `const CACHE = "droppix-shell-v4";` → `"droppix-shell-v5";` (forces existing PWA installs to re-precache the new `index.html`/shell).

- [ ] **Step 2: Rebuild `web/dist` off-mount and sync it back:**
```bash
# off-mount build (recipe) produces ~/droppix-web-build/dist
cp -r "/var/mnt/nas/Projects/Spacedesk for linux/web/"{src,public,tests,build.mjs} ~/droppix-web-build/ && \
cd ~/droppix-web-build && npm run build && \
rm -rf "/var/mnt/nas/Projects/Spacedesk for linux/web/dist" && \
cp -r ~/droppix-web-build/dist "/var/mnt/nas/Projects/Spacedesk for linux/web/dist"
```
Verify: `grep -c "btn-connect" web/dist/index.html` ≥ 1 and `grep "droppix-shell-v5" web/dist/sw.js`.

- [ ] **Step 3: Update docs** — `docs/STATUS.md`: update the "Web PWA client" row to note the redesigned shell (PIN-entry connect, in-session control bar, settings drawer, dark/light theme); bump Last verified to 2026-08-03. Flip the spec `docs/superpowers/specs/2026-08-03-web-client-redesign-design.md` Status header to `**Status:** Shipped on master (2026-08-03).`

- [ ] **Step 4: Final build + tests** — off-mount `npm test` green + `npm run build` clean.

- [ ] **Step 5: Commit**
```bash
git add web/public/sw.js web/dist docs/STATUS.md docs/superpowers/specs/2026-08-03-web-client-redesign-design.md
git commit -m "chore(web): bump sw cache v5, rebuild dist, docs (redesign shipped)"
```

---

## Self-Review

**Spec coverage:** Connect screen + PIN entry → Tasks 1,3. Local-match validation → Tasks 1,3 (`pinMatches` vs served `pairingCode`, mock bypass). In-session auto-hiding control bar → Task 4. Settings drawer surfacing all localStorage settings → Task 5. Dark/light theme + persist → Tasks 2,3,5. Module split (`pin`/`theme`/`connect-view`/`session-controls`/`settings-drawer` + slim `main.ts`) → Tasks 1–5. Preserved transport/decoder/audio/input + IDs → Global Constraints, honored in Task 3. sw bump + dist rebuild + docs → Task 6. Responsive side-drawer⇢bottom-sheet → Task 5. Testing (pure PIN sliver + keep green + esbuild) → Tasks 1,2 + build gates. No gaps.

**Placeholder scan:** No TBD/TODO; full code for `pin.ts`/`theme.ts`/`connect-view.ts`/tests; markup/style tasks give the element/ID contract + the must-preserve IDs + concrete wiring code. The Task-4 `openDrawer` stub is explicitly a stub replaced in Task 5 (named, not vague).

**Type consistency:** `Theme` (`theme.ts`) shared by `settings.ts` (type-only import, no cycle), `main.ts`, drawer. `pinMatches/normalizePin/pinComplete` names consistent across Tasks 1/3. `ConnectView(onConnect)`, `SessionControls(opts)`, `SettingsDrawer(onChange)` ctor shapes match their wiring in `main.ts`. DOM IDs (`#app`,`#pin`,`#btn-connect`,`#c-status`,`#controls`,`#drawer`,`#scrim`,`video`,`stage`,`mock-backdrop`,`click-layer`,`mock-log`,`hud`) consistent across the markup and the modules that query them.
