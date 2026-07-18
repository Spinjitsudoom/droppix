import { test } from "node:test";
import assert from "node:assert/strict";
import { contentBox, normalizePointer } from "../src/fit.ts";

test("contain letterboxes and rejects outside clicks", () => {
  const box = contentBox(200, 100, 100, 100, "contain");
  assert.equal(box.w, 100);
  assert.equal(box.h, 100);
  assert.equal(box.x, 50);
  const mid = normalizePointer(100, 50, box, false);
  assert.ok(mid);
  assert.equal(mid!.x, 32768); // ~0.5 * 65535
  assert.equal(normalizePointer(10, 50, box, false), null);
});

test("cover fills and clamps outside", () => {
  const box = contentBox(200, 100, 100, 100, "cover");
  assert.equal(box.h, 200);
  const n = normalizePointer(0, 0, box, true);
  assert.ok(n);
});
