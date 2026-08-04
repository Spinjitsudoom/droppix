import { test, expect } from "@playwright/test";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(__dirname, "..", "e2e-artifacts");

async function fetchInputs(request, { clear = false } = {}) {
  const q = clear ? "?clear=1" : "";
  const r = await request.get(`/debug/inputs${q}`);
  expect(r.ok()).toBeTruthy();
  return (await r.json()).inputs;
}

function kinds(inputs) {
  return new Set(inputs.map((i) => i.kind));
}

/**
 * The in-session control bar auto-hides (opacity:0, pointer-events:none)
 * 2.5s after the last pointer activity over #stage (see session-controls.ts).
 * A stray pointer move over the video re-arms it just before we click one of
 * its buttons, so a slow poll earlier in the test can't leave it un-clickable.
 */
async function wakeControls(page, box) {
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.5);
  await page.mouse.move(box.x + box.width * 0.5 + 3, box.y + box.height * 0.5 + 3);
}

test.describe("droppix web PWA vs mock host", () => {
  test("connect, stream, input, fit, mute", async ({ page, request }) => {
    const consoleErrors = [];
    page.on("console", (msg) => {
      if (msg.type() === "error") consoleErrors.push(msg.text());
    });

    await page.goto("/", { waitUntil: "domcontentloaded" });
    await fetchInputs(request, { clear: true });

    await expect(page.locator("#mock-badge")).toBeVisible();
    // Disconnected/idle: no local mock wallpaper - black stage only.
    await expect(page.locator("#mock-backdrop")).toBeHidden();

    // Mock mode (config.json mock:true) skips the PIN-entry connect screen
    // entirely and auto-connects (main.ts loadConfig); wait for the stream.
    await expect(page.locator('[data-act="disconnect"]')).toBeVisible({ timeout: 10_000 });
    await expect(page.locator("#status-pill")).toContainText(/Streaming \d+x\d+/, {
      timeout: 15_000,
    });

    // WebCodecs painted testsrc onto the canvas
    const canvas = page.locator("#video");
    await expect
      .poll(
        async () =>
          page.evaluate(() => {
            const c = document.getElementById("video");
            if (!(c instanceof HTMLCanvasElement) || c.width < 2 || c.height < 2) return 0;
            const ctx = c.getContext("2d");
            if (!ctx) return 0;
            const { data } = ctx.getImageData(0, 0, c.width, c.height);
            let lit = 0;
            for (let i = 0; i < data.length; i += 16) {
              if (data[i] + data[i + 1] + data[i + 2] > 30) lit++;
            }
            return lit;
          }),
        { timeout: 20_000, message: "canvas never received decoded video pixels" },
      )
      .toBeGreaterThan(50);

    // Mock starts muted. Simulate the real autoplay policy (bundled Chromium
    // doesn't enforce it) by forcing the context suspended.
    await page.evaluate(() => window.__droppixSuspendAudio?.());
    await expect
      .poll(async () => page.evaluate(() => window.__droppixDebug?.()?.audio?.state), {
        timeout: 3_000,
        message: "test hook failed to suspend AudioContext",
      })
      .toBe("suspended");

    const box = await canvas.boundingBox();
    expect(box).toBeTruthy();

    await request.get("/debug/server-marks?clear=1");

    await canvas.click({ position: { x: box.width * 0.4, y: box.height * 0.4 } });
    await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.5);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.55, { steps: 5 });
    await page.mouse.up();

    await canvas.click({
      button: "right",
      position: { x: box.width * 0.3, y: box.height * 0.3 },
    });

    await canvas.hover({ position: { x: box.width * 0.5, y: box.height * 0.5 } });
    await page.mouse.wheel(0, 120);

    await canvas.focus();
    await page.keyboard.press("a");
    await page.keyboard.press("ArrowRight");

    await expect
      .poll(
        async () => {
          const got = kinds(await fetchInputs(request));
          return ["Touch", "MouseButton", "Scroll", "Key"].every((k) => got.has(k))
            ? "ok"
            : [...got].join(",");
        },
        { timeout: 5_000, message: "mock host missing Touch/MouseButton/Scroll/Key" },
      )
      .toBe("ok");

    // Clicks above are a user gesture, so the AudioContext must resume even
    // though it was suspended - proving the autoplay recovery path works.
    await expect
      .poll(
        async () => page.evaluate(() => window.__droppixDebug?.()?.audio?.state),
        { timeout: 5_000, message: "AudioContext never resumed after gesture" },
      )
      .toBe("running");

    // Unmute (mock starts muted) and confirm PCM packets actually flow. Mute
    // is now a control-bar toggle button (data-act="mute"), not a checkbox;
    // wake the auto-hiding control bar first so the click actually lands.
    await wakeControls(page, box);
    await page.locator('[data-act="mute"]').click();
    await expect
      .poll(
        async () => page.evaluate(() => window.__droppixDebug?.()?.audio?.packets ?? 0),
        { timeout: 5_000, message: "no audio packets flowing after unmute" },
      )
      .toBeGreaterThan(10);

    // Server app applied input into its framebuffer (E2E, not local CSS).
    await expect
      .poll(
        async () => {
          const r = await request.get("/debug/server-marks");
          const j = await r.json();
          const marks = j.marks || [];
          return marks.some((m) => m.kind === "touch" || m.kind === "left" || m.kind === "right")
            ? marks.length
            : 0;
        },
        { timeout: 5_000, message: "server desktop got no click marks" },
      )
      .toBeGreaterThan(0);

    // Fit and audio are no longer toolbar controls - both moved into the
    // settings drawer, opened from the control bar's gear button (the
    // topbar's #btn-settings is hidden while data-view="session").
    await wakeControls(page, box);
    await page.locator('[data-act="settings"]').click();
    await expect(page.locator("#app")).toHaveClass(/drawer-open/);

    for (const mode of ["cover", "stretch", "contain"]) {
      const seg = page.locator(`#set-fit .seg-btn[data-value="${mode}"]`);
      await seg.click();
      await expect(seg).toHaveAttribute("aria-pressed", "true");
    }

    // Drawer's Audio checkbox mirrors the control bar's mute toggle. It
    // should read checked (audio enabled) after the unmute above.
    await expect(page.locator("#set-audio")).toBeChecked();
    await page.locator("#set-audio").uncheck();
    await expect(page.locator("#set-audio")).not.toBeChecked();
    await page.locator("#set-audio").check();
    await expect(page.locator("#set-audio")).toBeChecked();

    await page.locator("#drawer-close").click();
    await expect(page.locator("#app")).not.toHaveClass(/drawer-open/);

    await page.screenshot({
      path: path.join(OUT, "streaming.png"),
      fullPage: true,
    });

    // Screenshot + drawer close above may have idled the control bar out;
    // wake it before the disconnect click.
    await wakeControls(page, box);
    await page.locator('[data-act="disconnect"]').click();
    await expect(page.locator("#btn-connect")).toBeVisible();
    await expect(page.locator("#status-pill")).toContainText(/Disconnected/i);

    // Regression: no stale frames may linger - canvas must be fully black.
    await expect
      .poll(
        async () =>
          page.evaluate(() => {
            const c = document.getElementById("video");
            if (!(c instanceof HTMLCanvasElement) || c.width < 2) return -1;
            const ctx = c.getContext("2d");
            if (!ctx) return -1;
            const { data } = ctx.getImageData(0, 0, c.width, c.height);
            let lit = 0;
            for (let i = 0; i < data.length; i += 16) {
              if (data[i] + data[i + 1] + data[i + 2] > 30) lit++;
            }
            return lit;
          }),
        { timeout: 5_000, message: "canvas still shows video after disconnect" },
      )
      .toBe(0);

    // Regression (ghost stream): a leaked socket/ffmpeg would repaint the
    // canvas within a second - it must STAY black, and the server must agree
    // that no media session is active.
    await page.waitForTimeout(1_500);
    const litAfter = await page.evaluate(() => {
      const c = document.getElementById("video");
      if (!(c instanceof HTMLCanvasElement) || c.width < 2) return -1;
      const ctx = c.getContext("2d");
      if (!ctx) return -1;
      const { data } = ctx.getImageData(0, 0, c.width, c.height);
      let lit = 0;
      for (let i = 0; i < data.length; i += 16) {
        if (data[i] + data[i + 1] + data[i + 2] > 30) lit++;
      }
      return lit;
    });
    expect(litAfter, "ghost stream repainted canvas after disconnect").toBe(0);
    const sess = await (await request.get("/debug/session")).json();
    expect(sess.active, "server still has an active media session").toBe(false);
  });

  test("GEEKS server-side stats overlay + /debug/stats", async ({ page, request }) => {
    await page.goto("/", { waitUntil: "domcontentloaded" });
    await expect(page.locator('[data-act="disconnect"]')).toBeVisible({ timeout: 10_000 });
    await expect(page.locator("#status-pill")).toContainText(/Streaming \d+x\d+/, { timeout: 15_000 });

    // Wait for real decoded pixels so a session + stats exist.
    await expect
      .poll(
        async () =>
          page.evaluate(() => {
            const c = document.getElementById("video");
            if (!(c instanceof HTMLCanvasElement) || c.width < 2) return 0;
            const ctx = c.getContext("2d");
            const { data } = ctx.getImageData(0, 0, c.width, c.height);
            let lit = 0;
            for (let i = 0; i < data.length; i += 16) if (data[i] + data[i + 1] + data[i + 2] > 30) lit++;
            return lit;
          }),
        { timeout: 20_000 },
      )
      .toBeGreaterThan(50);

    // Toggle GEEKS server-side via the 'G' shortcut (same effect as the button).
    await page.locator("#video").focus();
    await page.keyboard.press("g");
    await expect
      .poll(async () => (await request.get("/debug/stats")).json().then((j) => j.geeks), {
        timeout: 5_000,
        message: "server did not enable GEEKS overlay",
      })
      .toBe(true);

    // The burned-in stats reflect the real negotiated protocol.
    const s = await (await request.get("/debug/stats")).json();
    expect(s.protocol).toBe(5);
    expect(s.hello?.version).toBe(5);
    expect(s.config?.width).toBeGreaterThan(0);
    expect(s.config?.fps).toBeGreaterThan(0);
    expect(s.out?.Video).toBeGreaterThan(0);
    expect(s.out?.Config).toBe(1);
    await expect
      .poll(async () => (await request.get("/debug/stats")).json().then((j) => j.ping?.samples ?? 0), {
        timeout: 5_000,
        message: "no server ping/pong RTT samples",
      })
      .toBeGreaterThan(0);

    // The overlay is burned into the video, so it appears on-canvas only after
    // encode+network+decode latency. The full-screen stats panel darkens the
    // centre (normally the bright movie) - wait for that before screenshotting.
    await expect
      .poll(
        async () =>
          page.evaluate(() => {
            const c = document.getElementById("video");
            const ctx = c.getContext("2d");
            const s = 80;
            const { data } = ctx.getImageData((c.width - s) >> 1, (c.height - s) >> 1, s, s);
            let sum = 0;
            for (let i = 0; i < data.length; i += 4) sum += data[i] + data[i + 1] + data[i + 2];
            return sum / (data.length / 4); // avg brightness 0..765
          }),
        { timeout: 6_000, message: "GEEKS stats panel never darkened the centre" },
      )
      .toBeLessThan(180);

    await page.waitForTimeout(800);
    await page.screenshot({ path: path.join(OUT, "geeks.png"), fullPage: true });
    await page.waitForTimeout(800);

    // Toggle back off.
    await page.keyboard.press("g");
    await expect
      .poll(async () => (await request.get("/debug/stats")).json().then((j) => j.geeks), {
        timeout: 5_000,
      })
      .toBe(false);
  });
});
