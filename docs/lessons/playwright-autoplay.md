# G-2026-07-18-playwright-autoplay: Playwright Chromium never suspends AudioContext

- **ID:** `G-2026-07-18-playwright-autoplay`
- **Tags:** `client`, `audio`, `flake`, `wrong-answer`, `high`, `gotcha`
- **Date:** 2026-07-18
- **Related:** `L-2026-07-18-delta-drop-corruption`, `L-2026-07-18-ghost-ws-stream`

## Symptom

E2E suite passed while a real Chrome user got no audio and the console warned: "The AudioContext was not allowed to start. It must be resumed (or created) after a user gesture" (`web/src/audio.ts` unlock). Root scenario: the mock auto-connects from a `setTimeout`, which is not a user gesture, so real Chrome kept the context suspended.

**Worse follow-on:** the whole client then appeared dead - Connect did nothing, no video. `await ctx.resume()` returns a promise that stays **pending** (never resolves, never rejects) until a gesture under the autoplay policy. `connect()` awaited `audio.unlock()` which awaited that resume, so the entire connect flow hung and the `connecting` single-flight flag stayed `true`, making every later Connect click a no-op. Playwright never caught it because its bundled Chromium resolves resume() immediately.

## Root cause

Playwright's bundled Chromium does not enforce the autoplay policy. `new AudioContext().state` is `running` immediately - headless or headed, on `about:blank` or a real https origin, even with `--autoplay-policy=user-gesture-required` and with Playwright's own `--autoplay-policy=no-user-gesture-required` removed via `ignoreDefaultArgs`. The flag is simply ineffective in the bundled build, so no launch configuration reproduces real-Chrome suspension.

## Fix

Two parts (2026-07-18):

- Client (`web/src/audio.ts`): **never `await` `resume()`** - fire it with `void ctx.resume().catch(()=>{})` and arm one-shot capture-phase `pointerdown`/`keydown` listeners that resume it; re-arm from `onstatechange` whenever it suspends again. Status line shows "tap for audio" while suspended. `connect()` in `main.ts` wraps init in `try/finally` so `connecting` always resets even if audio init throws.
- Test: since the policy cannot be enabled, simulate it. `main.ts` exposes `window.__droppixSuspendAudio()` (calls `ctx.suspend()`); the spec suspends the context after connect, performs the normal input clicks, then asserts `__droppixDebug().audio` reaches `state: "running"` with packets flowing.

## How to detect this in the future

- Any Web Audio feature "verified" only by Playwright is unverified for autoplay: assert on `AudioContext.state`, not just packet counters.
- Manual check: open the mock in real Chrome, don't click anything, look for the autoplay warning in the console.
- Anti-pattern signature: `await someAudioCtx.resume()` on any code path that gates connect/stream startup. Under autoplay this awaits forever. Treat `resume()` as fire-and-forget.
