import { test } from "node:test";
import assert from "node:assert/strict";
import { nextTheme } from "../src/theme.ts";
test("nextTheme flips", () => {
  assert.equal(nextTheme("dark"), "light");
  assert.equal(nextTheme("light"), "dark");
});
