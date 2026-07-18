import { test } from "node:test";
import assert from "node:assert/strict";
import { codeToEvdev } from "../src/keymap.ts";

test("maps letters and arrows like Android KeyMap", () => {
  assert.equal(codeToEvdev("KeyA"), 30);
  assert.equal(codeToEvdev("KeyQ"), 16);
  assert.equal(codeToEvdev("Enter"), 28);
  assert.equal(codeToEvdev("Escape"), 1);
  assert.equal(codeToEvdev("ArrowUp"), 103);
  assert.equal(codeToEvdev("F1"), 59);
  assert.equal(codeToEvdev("Space"), 57);
});

test("unknown code is 0", () => {
  assert.equal(codeToEvdev("AudioVolumeUp"), 0);
});
