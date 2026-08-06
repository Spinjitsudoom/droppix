import { test } from "node:test";
import assert from "node:assert/strict";
import { resolveResolution } from "../src/settings.ts";

const AUTO = { w: 2340, h: 1080 };

test("fixed resolution wins over the auto fallback", () => {
  assert.deepEqual(resolveResolution("1280x720", AUTO), { w: 1280, h: 720 });
});

test('"auto" falls back to the canvas-derived size', () => {
  assert.deepEqual(resolveResolution("auto", AUTO), AUTO);
});

test("unparseable values fall back to auto (no crash)", () => {
  assert.deepEqual(resolveResolution("", AUTO), AUTO);
  assert.deepEqual(resolveResolution("720p", AUTO), AUTO);
  assert.deepEqual(resolveResolution("1280x", AUTO), AUTO);
});

test("odd dimensions round down to even (H.264 needs even w/h)", () => {
  assert.deepEqual(resolveResolution("1281x721", AUTO), { w: 1280, h: 720 });
});

test("degenerate tiny sizes fall back to auto", () => {
  assert.deepEqual(resolveResolution("0x0", AUTO), AUTO);
  assert.deepEqual(resolveResolution("1x1", AUTO), AUTO);
});
