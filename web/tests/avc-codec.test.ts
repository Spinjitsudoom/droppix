import { test } from "node:test";
import assert from "node:assert/strict";
import { avcCodecString } from "../src/avc-codec.ts";

// Annex-B helper: concatenate [start code][nal bytes] units.
function annexB(...nals: number[][]): Uint8Array {
  const out: number[] = [];
  for (const n of nals) {
    out.push(0, 0, 0, 1);
    out.push(...n);
  }
  return new Uint8Array(out);
}

test("derives avc1 string from a Baseline L4.0 SPS", () => {
  // SPS NAL: header 0x67 (type 7), then profile_idc=0x42, constraints=0xE0, level_idc=0x28 (40).
  const nal = annexB([0x67, 0x42, 0xe0, 0x28, 0x11, 0x22]);
  assert.equal(avcCodecString(nal), "avc1.42E028");
});

test("derives High-profile L3.1 SPS", () => {
  const nal = annexB([0x67, 0x64, 0x00, 0x1f, 0xac, 0x00]);
  assert.equal(avcCodecString(nal), "avc1.64001F");
});

test("finds the SPS among AUD/SEI/PPS/IDR NALs", () => {
  const nal = annexB(
    [0x09, 0x10], // AUD (type 9)
    [0x67, 0x42, 0xc0, 0x1f, 0x00], // SPS (type 7), Constrained Baseline L3.1
    [0x68, 0xce, 0x3c, 0x80], // PPS (type 8)
    [0x65, 0x88, 0x84], // IDR slice (type 5)
  );
  assert.equal(avcCodecString(nal), "avc1.42C01F");
});

test("handles the 3-byte start code (00 00 01)", () => {
  const nal = new Uint8Array([0, 0, 1, 0x67, 0x4d, 0x40, 0x1e, 0x00]); // Main L3.0
  assert.equal(avcCodecString(nal), "avc1.4D401E");
});

test("returns null when there is no SPS", () => {
  const nal = annexB([0x65, 0x88, 0x84], [0x68, 0xce]); // only IDR + PPS
  assert.equal(avcCodecString(nal), null);
});

test("returns null on a truncated SPS", () => {
  const nal = annexB([0x67, 0x42]); // header + profile only, no level
  assert.equal(avcCodecString(nal), null);
});
