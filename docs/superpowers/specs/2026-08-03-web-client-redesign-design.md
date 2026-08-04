# Web PWA client redesign (spacedesk language + PIN entry + settings drawer)

**Date:** 2026-08-03
**Status:** Shipped on master (2026-08-04).
**Roadmap:** UI overhaul — the **web-client** half (host GUI shipped 2026-08-03, see `2026-08-03-host-gui-redesign-design.md`). Carries the same spacedesk visual language to `web/`.
**Mockup:** approved interactive preview — artifact `5ffdf743-d81e-4a17-bf9e-d059677b0ed3` (connect screen with 6-digit PIN entry, in-session auto-hiding control bar, settings drawer, dark/light toggle).

## Summary

Rebuild the web PWA client's **UI shell** into a clean two-state app that matches the redesigned host: a **connect screen** with an Android-style 6-digit **PIN entry** (replacing the "PIN matches PC" checkbox), an **in-session** view with an auto-hiding player-style **control bar**, and a **settings drawer** that finally surfaces the settings hidden in localStorage today (quality, fps, audio, fit, flip, brightness, contrast, wall, device name) plus a new **dark/light theme** toggle. This is a **UI-shell + settings-surface + theming change only** — the transport, decoder, audio, input, fullscreen, mock, and the wire protocol are untouched. Stays vanilla TypeScript + esbuild (no framework).

## Reference & visual direction

Same **spacedesk language** as the host redesign: flat cards, teal accent `#14b8a6`, generous spacing, dark **and** light palettes sharing one token set. The connect card echoes the host's Status hero; the settings drawer echoes the host's Settings section. **Balanced/responsive:** a centered card + right-side drawer on a desktop browser; full-bleed + a bottom-sheet drawer on a tablet.

## Decisions

| Question | Decision |
| --- | --- |
| Depth | Restructure **and** polish (not paint-only). |
| Theme | **Both** dark + light, sharing the host's palette; persisted in localStorage; a toggle in the header and in the drawer. |
| Ergonomics | Balanced/responsive — desktop tab and tablet browser both first-class; side-drawer ⇢ bottom-sheet at a breakpoint. |
| Pairing UX | **6-digit PIN entry** (segmented, auto-advancing) replacing the "PIN matches PC" checkbox. The code is **no longer shown on the page**; the user types what the PC shows. |
| PIN validation | **Local match** against the `pairingCode` the host already serves in `config.json` — a "you're looking at the right PC" gesture matching Android, **no wire/protocol change**. Server-side PIN verification is explicitly out of scope (the web path is already on host-served HTTPS/WSS). |

## Scope / non-goals

**In scope:** `web/public/index.html` (markup), `web/public/styles.css` (full restyle, dual theme, responsive), `web/src/main.ts` (orchestration for the new shell), a small set of new `web/src/*.ts` UI modules, `web/src/settings.ts` (+`theme` field), `web/public/sw.js` (cache bump), and the committed `web/dist` rebuild.

**Out of scope (explicit):**
- **No wire/protocol change.** `web/src/protocol.ts` and `transport.ts` HELLO/framing are untouched (still HELLO v6 from the wall-grid work).
- **No server-side PIN auth** — local match only (see Decisions).
- **No change** to `decoder.ts`, `audio.ts`, `audio-worklet.ts`, `input.ts`, `fit.ts`, `fullscreen.ts`, `keymap.ts`, `mock-overlay.ts` — the streaming/decoding/input path is preserved.
- No new streaming features; no host change; no framework.

## Architecture

Keep vanilla TS + esbuild. `main.ts` today is a ~300-line god-file that would grow with the new shell, so extract the shell into focused modules (matching the existing flat `web/src/*.ts` convention):

- **`web/src/theme.ts`** — `type Theme = 'dark' | 'light'`; `applyTheme(t)` sets `data-theme` on `document.documentElement`; `initTheme()` reads `settings.theme` and applies; `toggleTheme()` flips + persists via `saveSettings`.
- **`web/src/connect-view.ts`** — builds/wires the 6-digit PIN entry (auto-advance, backspace-back, numeric-only), enables **Connect** at 6 digits, and calls a supplied `onConnect(code)` callback. Exposes `showError()` and `reset()`.
- **`web/src/session-controls.ts`** — the in-session control bar: button callbacks (disconnect/fullscreen/mute/fit/HUD/settings) + an auto-hide timer (hide after ~2.5 s idle; show on `pointermove`/tap).
- **`web/src/settings-drawer.ts`** — opens/closes the drawer (scrim, ESC, responsive side⇢sheet via CSS) and binds each field to `ClientSettings` (load on open, save + apply-live on change).
- **`main.ts`** — orchestrator: `loadConfig()`, construct the unchanged `Transport`/`VideoPipeline`/`AudioPlayer`/`InputBinder`, switch the view disconnected⇄connected, wire the connect callback → `transport.connect(...)`, and route drawer changes to live setters (`video.setAdjust`, `video.setFit`, `audio.setMuted`).

**View state** is a single attribute (`data-view="connect" | "session"` on the app root) toggled by `main.ts`; CSS shows/hides each section. No router, no framework.

## Connect flow (`connect-view.ts` + `main.ts`)

- `loadConfig()` still fetches `./config.json` → `{ pairingCode, mock, e2eDesktop, burnIn }`. The `pairingCode` is kept in memory (not rendered) as the local match target.
- **PIN entry:** six single-digit inputs; typing advances focus, backspace on an empty box retreats, non-digits rejected. **Connect** enables at 6 digits.
- **Connect:** if `entered === pairingCode` → proceed to `transport.connect(...)` and switch to the session view; else show the error state ("That code doesn't match your PC — check the screen") and keep the user on the connect screen.
- **Mock mode** (`config.json mock=true`) preserves today's behavior: skip PIN entry and auto-connect (no code to match on a mock host); the `MOCK` badge shows.
- All other connect-time behavior is preserved: audio unlock on the connect gesture, the reconnect/`onClose` status updates, the orphaned-socket guard, and `beforeinstallprompt` → Install button.

## Settings drawer (`settings-drawer.ts`)

Surface every `ClientSettings` field (from `settings.ts`), grouped:

- **Stream:** Quality (bitrate 4000/8000/16000 → Low/Medium/High), FPS (30/60), Audio (mute).
- **Display:** Fit (contain/cover/stretch), Flip, Brightness (−100…100), Contrast (0…200).
- **Wall position:** col / row (0-based numeric).
- **Device:** name (shown on the PC).
- **Appearance:** Theme (dark/light).

Changes persist immediately (`saveSettings`) and apply live where the pipeline supports it (`video.setAdjust(flip,brightness,contrast)`, `video.setFit`, `audio.setMuted`); quality/fps/name/wall apply on the next connect (documented in the drawer, matching how they work today). The drawer is reachable from the connect header **and** the in-session control bar.

## Theme system (`theme.ts` + `styles.css`)

- `styles.css` is restyled around a **CSS custom-property token set** on `:root` (dark) with a `:root[data-theme="light"]` override — same palette as the host (`--bg/--surface/--panel/--border/--text/--muted/--accent #14b8a6/--good/--warn/--bad`). Components read tokens only, so both themes derive from one sheet.
- `settings.ts` gains `theme: Theme` (default `'dark'`). `initTheme()` applies it before first paint; the toggle persists it. Existing users (no `theme` key) default to dark via the `loadSettings` defaults-merge.

## Error handling / preserved behavior

- `config.json` unavailable → the connect screen shows the existing "config.json unavailable — is the host serving with `--web`?" status.
- WebCodecs missing → the existing "use Chromium" status.
- Reconnect banner, host-`BYE`, and blank-canvas-on-close all preserved (they live in `main.ts`'s transport handlers, unchanged in behavior).
- **PWA:** `sw.js` bumps its shell cache (`droppix-shell-v4` → `v5`) because `index.html`/`styles.css` change; JS/CSS stay network-first as today, the shell is re-precached (see the committed-dist + sw-cache lesson).

## Testing

- Web tests run via `node --test` (`web/tests/*.test.ts`); keep them green.
- **Extract + test the pure sliver:** a `normalizePin(raw): string` / `pinComplete(code)` / `pinMatches(entered, served)` helper in `connect-view.ts` (or a tiny `pin.ts`) — unit-test digit-stripping, 6-digit completeness, and exact match. This is the one piece of real logic worth a test; the rest is DOM wiring.
- **Build gate:** `npm run build` (esbuild) clean; typecheck is via esbuild/bundler as today (no standalone `tsc` script).
- **web/dist:** rebuilt off-mount and committed (the CIFS mount can't host `node_modules`; `web/dist` is a committed packaging artifact — see the lesson).
- **Manual:** the `web-mock-host` skill serves a local mock host — verify PIN entry (wrong code errors, right code connects), the in-session control bar auto-hide, the settings drawer surfacing + live-apply (brightness/contrast/fit/flip), theme toggle persisting across reload, and the responsive side-drawer⇢bottom-sheet.

## Risks

| Risk | Mitigation |
| --- | --- |
| `main.ts` god-file grows | Extract the 4 focused shell modules above; `main.ts` becomes a slim orchestrator. |
| Behavior regression in connect/reconnect | Keep all transport handlers and mock/auto-connect logic in `main.ts` unchanged; only the view shell and the connect trigger change. |
| Stale PWA shell after redesign | Bump `sw.js` cache; rebuild + commit `web/dist`. |
| Light-theme contrast | Per-theme tokens; manual check on the light ground. |
| PIN-entry seen as security | Documented as a mis-connection guard (local match), not server auth. |

## Out of scope (reiterated)

Wire/protocol change, server-side PIN verification, streaming/decoder/audio/input changes, host changes, and any framework introduction.
