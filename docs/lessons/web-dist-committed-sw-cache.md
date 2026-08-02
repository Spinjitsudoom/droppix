# G-2026-08-02-web-dist-committed-sw-cache: web/dist is committed and the SW caches the shell

- **ID:** `G-2026-08-02-web-dist-committed-sw-cache`
- **Tags:** `client`, `packaging`, `gotcha`, `silent-failure`, `medium`
- **Date:** 2026-08-02
- **Related:** *(none)*

## Symptom

Two ways a web-client (`web/`) source change can "ship" but not actually reach users:

1. You edit `web/src/*.ts` / `web/public/*`, run tests, commit — but the Flatpak/AppImage
   build serves the **old UI** because it consumes the pre-built `web/dist/`, which you
   never rebuilt.
2. You rebuild `web/dist/` and deploy, but a returning PWA install still shows the **old
   `index.html`** (e.g. a new toolbar control is missing) even though the new `main.js`
   loaded — so the JS and the HTML disagree.

## Root cause

- **`web/dist/` is committed on purpose** (see `.gitignore`: "web/dist is committed so
  Flatpak/AppImage packaging can consume it without Node on the builder"). Editing
  `web/src`/`web/public` without rebuilding `dist` leaves packaging on stale bundles.
- The service worker `web/public/sw.js` serves the **shell (`./`, `index.html`, manifest,
  icons) cache-first** (`caches.match(...) => hit || fetch`). JS/CSS/`.map`/`config.json`
  are always network, so code updates land — but a changed `index.html` is pinned to the
  old cache until `const CACHE = "droppix-shell-vN"` is bumped (the `activate` handler only
  deletes caches whose key `!== CACHE`).

## Fix

When you change web sources:

1. Rebuild `dist` **off-mount** (the CIFS/NAS mount can't host `node_modules` — `npm ci`
   fails on `.bin` symlinks / esbuild's platform binary with `ENOTSUP`). Copy
   `src public tests build.mjs package*.json tsconfig.json` to `~/droppix-web-build`,
   `npm ci`, `npm run build`, then copy the resulting `dist/` back over `web/dist/`.
   Verify: `grep <new-symbol> web/dist/main.js`.
2. If you changed `web/public/index.html` (or any precached shell asset), **bump**
   `CACHE = "droppix-shell-vN"` in `web/public/sw.js` (and re-sync `dist/sw.js` via the
   rebuild) so existing installs re-precache the new shell.
3. Verification is `npm test` (Node `--experimental-strip-types`, no bundler) + `npm run
   build` (esbuild). There is **no standalone `tsc` script**; bare `tsc --noEmit` reports
   false errors (`.ts` import extensions, missing `@types/node`) because the project relies
   on esbuild/bundler resolution, not `tsc`, for its build.

## How to detect this in the future

- Web source diff with **no `web/dist/` change in the same commit** → you forgot to rebuild.
- `index.html`/precached-shell change with **no `sw.js` `CACHE` bump** → returning PWA
  users keep the old shell.
