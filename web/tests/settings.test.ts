import { test } from "node:test";
import assert from "node:assert/strict";
import { resolveResolution } from "../src/settings.ts";

// A Redmi 9's panel: 19.5:9, NOT 16:9. The old fixed presets assumed 16:9 and made the
// host render the wrong shape, which then had to be letterboxed or stretched.
const AUTO = { w: 2340, h: 1080 };
const ASPECT = AUTO.w / AUTO.h;

test("a preset preserves the DEVICE aspect ratio instead of forcing 16:9", () => {
  const r = resolveResolution("720", AUTO);
  assert.equal(r.h, 720);
  // 720 * (2340/1080) = 1560, not the 1280 a fixed 16:9 preset would give.
  assert.equal(r.w, 1560);
  assert.ok(Math.abs(r.w / r.h - ASPECT) < 0.01, "aspect must match the device");
});

test("every preset keeps the aspect, so nothing ever stretches", () => {
  for (const preset of ["1080", "720", "540", "480"]) {
    const r = resolveResolution(preset, AUTO);
    assert.ok(
      Math.abs(r.w / r.h - ASPECT) < 0.01,
      `${preset} gave ${r.w}x${r.h}, aspect ${(r.w / r.h).toFixed(3)} != ${ASPECT.toFixed(3)}`,
    );
  }
});

test("a 16:9 device still gets 16:9 output", () => {
  const laptop = { w: 1920, h: 1080 };
  const r = resolveResolution("720", laptop);
  assert.deepEqual(r, { w: 1280, h: 720 });
});

test("legacy WxH settings are honoured by their height, re-derived to the real aspect", () => {
  // Settings saved by an older build must not resurrect the wrong shape.
  const r = resolveResolution("1280x720", AUTO);
  assert.equal(r.h, 720);
  assert.equal(r.w, 1560);
});

test("never upscales past the device", () => {
  assert.deepEqual(resolveResolution("1080", AUTO), AUTO);   // device is only 1080 tall
  assert.deepEqual(resolveResolution("2160", AUTO), AUTO);
});

test('"auto" uses the canvas-derived size', () => {
  assert.deepEqual(resolveResolution("auto", AUTO), AUTO);
});

test("unparseable values fall back to auto (no crash)", () => {
  assert.deepEqual(resolveResolution("", AUTO), AUTO);
  assert.deepEqual(resolveResolution("abc", AUTO), AUTO);
  assert.deepEqual(resolveResolution("0", AUTO), AUTO);
});

test("dimensions stay even (H.264 requires it)", () => {
  // 1077-tall device: an odd aspect that would otherwise produce odd numbers.
  const odd = { w: 2337, h: 1077 };
  for (const preset of ["720", "540", "480"]) {
    const r = resolveResolution(preset, odd);
    assert.equal(r.w % 2, 0, `${preset} width ${r.w} must be even`);
    assert.equal(r.h % 2, 0, `${preset} height ${r.h} must be even`);
  }
});

test("degenerate device sizes fall back rather than dividing by zero", () => {
  assert.deepEqual(resolveResolution("720", { w: 0, h: 0 }), { w: 0, h: 0 });
});
