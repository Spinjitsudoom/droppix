import { test } from "node:test";
import assert from "node:assert/strict";
import {
  scheduleAudio,
  kMaxAudioLeadSec,
  kAudioResyncLeadSec,
  kAudioMinLeadSec,
} from "../src/audio-policy.ts";

test("continues an existing schedule while the lead is healthy", () => {
  const now = 10;
  const next = now + 0.1;                    // 100 ms booked ahead
  const s = scheduleAudio(next, now);
  assert.equal(s.startAt, next, "must stay gapless, not nudge");
  assert.equal(s.resynced, false);
});

test("nudges forward instead of scheduling in the past", () => {
  const now = 10;
  const s = scheduleAudio(now - 5, now);     // schedule fell behind the clock
  assert.equal(s.startAt, now + kAudioMinLeadSec);
  assert.equal(s.resynced, false);
});

// The bug this exists for: a burst pushed nextTime seconds ahead and nothing pulled it back,
// so audio stayed seconds late for the rest of the session.
test("resyncs when the lead has run away", () => {
  const now = 10;
  const s = scheduleAudio(now + 3, now);     // 3 s booked ahead
  assert.equal(s.resynced, true);
  assert.ok(s.startAt <= now + kAudioResyncLeadSec + 1e-9);
  assert.ok(s.startAt - now < kMaxAudioLeadSec, "must land well inside the budget");
});

test("resyncing lands somewhere a later call treats as healthy", () => {
  const now = 10;
  const s = scheduleAudio(now + 5, now);
  // Feeding the resynced cursor straight back in must not immediately resync again, or the
  // stream would stutter continuously instead of settling.
  const again = scheduleAudio(s.startAt, now);
  assert.equal(again.resynced, false);
});

test("lead exactly at the cap is not a resync", () => {
  const now = 10;
  assert.equal(scheduleAudio(now + kMaxAudioLeadSec, now).resynced, false);
});

test("a seconds-long backlog collapses in one step, not gradually", () => {
  let next = 10 + 8;                          // 8 s behind
  const now = 10;
  const s = scheduleAudio(next, now);
  assert.ok(s.startAt - now <= kAudioResyncLeadSec + 1e-9,
    "one correction must fix it; bleeding down slowly still sounds late");
});
