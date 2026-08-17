import { test } from "node:test";
import assert from "node:assert/strict";
import { mergeHostSettings, loadSettings } from "../src/settings.ts";

// loadSettings() touches localStorage; stub just enough to get a defaults object.
(globalThis as unknown as { localStorage: unknown }).localStorage = {
  getItem: () => null,
  setItem: () => {},
};
(globalThis as unknown as { crypto: Crypto }).crypto ??= {
  getRandomValues: (a: Uint8Array) => a,
} as unknown as Crypto;

const base = loadSettings();

test("adopts stored values over the local copy", () => {
  const merged = mergeHostSettings(base, JSON.stringify({ fps: 30, resolution: "1080" }));
  assert.equal(merged.fps, 30);
  assert.equal(merged.resolution, "1080");
});

test("keeps local fields the blob does not mention", () => {
  const merged = mergeHostSettings({ ...base, name: "Desk PC" }, JSON.stringify({ fps: 30 }));
  assert.equal(merged.name, "Desk PC");
});

// Identity must not travel: `id` is how the host's approved-device store recognises this
// browser, so adopting another device's would inherit its approval.
test("never adopts the host blob's id", () => {
  const mine = { ...base, id: "mine" };
  const merged = mergeHostSettings(mine, JSON.stringify({ id: "someone-else", fps: 30 }));
  assert.equal(merged.id, "mine");
  assert.equal(merged.fps, 30, "the rest of the blob must still apply");
});

test("malformed or empty blobs leave settings untouched", () => {
  for (const bad of ["", "not json", "null", "[]", "{}", '"a string"', "123"]) {
    assert.deepEqual(mergeHostSettings(base, bad), base, `blob: ${bad}`);
  }
});

test("an id-only blob changes nothing", () => {
  assert.deepEqual(mergeHostSettings(base, JSON.stringify({ id: "x" })), base);
});
