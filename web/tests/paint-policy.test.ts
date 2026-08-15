import { test } from "node:test";
import assert from "node:assert/strict";
import {
  choosePaintIndex,
  kMaxPendingFrames,
  kMaxVideoLeadUs,
} from "../src/paint-policy.ts";

test("no frames: nothing to paint", () => {
  assert.equal(choosePaintIndex([], null), -1);
  assert.equal(choosePaintIndex([], 1000), -1);
});

test("without an audio clock, paint the newest frame immediately", () => {
  // Live desktop with audio off: latency is all that matters.
  assert.equal(choosePaintIndex([100, 200, 300], null), 2);
});

test("paints the newest frame the clock has reached, skipping stale ones", () => {
  assert.equal(choosePaintIndex([100, 200, 300], 250), 1);
  assert.equal(choosePaintIndex([100, 200, 300], 300), 2);
});

test("waits while video leads audio only slightly (lipsync preserved)", () => {
  // 10ms ahead: worth waiting for, keeps A/V aligned.
  assert.equal(choosePaintIndex([110_000], 100_000), -1);
});

test("stops waiting once the lead exceeds the bound (desktop stays responsive)", () => {
  // 40ms ahead: waiting would add 40ms of pointer lag — paint now instead.
  assert.equal(choosePaintIndex([140_000], 100_000), 0);
});

test("the bound is the boundary, not an approximation", () => {
  const clock = 1_000_000;
  // Exactly at the bound: still wait.
  assert.equal(choosePaintIndex([clock + kMaxVideoLeadUs], clock), -1);
  // One microsecond past: paint.
  assert.equal(choosePaintIndex([clock + kMaxVideoLeadUs + 1], clock), 0);
});

test("when it gives up waiting it paints the NEWEST frame, not the oldest", () => {
  // A live desktop should show current state, not replay a backlog.
  const clock = 0;
  const ts = [100_000, 200_000, 300_000];
  assert.equal(choosePaintIndex(ts, clock), 2);
});

test("a stalled audio clock cannot freeze video indefinitely", () => {
  // The regression this guards: audio stops advancing, video used to wait forever.
  const stalled = 500_000;
  let painted = 0;
  for (let i = 1; i <= 10; i++) {
    const ts = [stalled + i * 20_000];
    if (choosePaintIndex(ts, stalled) >= 0) painted++;
  }
  assert.ok(painted > 0, "video must keep painting when the audio clock stalls");
});

test("queue depth is shallow enough to not be a latency source", () => {
  // 12 frames was 200ms at 60fps of pure queueing delay.
  assert.ok(kMaxPendingFrames <= 3, "deep queues are latency on a live stream");
  const worstCaseMsAt60 = (kMaxPendingFrames / 60) * 1000;
  assert.ok(worstCaseMsAt60 <= 50, `queue adds ${worstCaseMsAt60}ms at 60fps`);
});
