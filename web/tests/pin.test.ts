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
