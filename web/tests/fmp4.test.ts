import { test } from "node:test";
import assert from "node:assert/strict";
import {
  parseAnnexB,
  extractParamSets,
  annexBToAvcc,
  buildInit,
  buildSegment,
} from "../src/fmp4.ts";

// Synthetic Annex-B access unit: SPS(7) + PPS(8) + IDR(5), 4-byte start codes.
const SC = [0, 0, 0, 1];
const SPS = [0x67, 0x42, 0xc0, 0x1f, 0xaa, 0xbb]; // profile 0x42, compat 0xc0, level 0x1f
const PPS = [0x68, 0xce, 0x3c, 0x80];
const IDR = [0x65, 0x88, 0x84, 0x00, 0x11, 0x22, 0x33];
const AU = new Uint8Array([...SC, ...SPS, ...SC, ...PPS, ...SC, ...IDR]);

function readBox(buf: Uint8Array, off: number): { size: number; type: string; body: number } {
  const size = (buf[off]! << 24) | (buf[off + 1]! << 16) | (buf[off + 2]! << 8) | buf[off + 3]!;
  const type = String.fromCharCode(buf[off + 4]!, buf[off + 5]!, buf[off + 6]!, buf[off + 7]!);
  return { size: size >>> 0, type, body: off + 8 };
}
function topLevelTypes(buf: Uint8Array): string[] {
  const types: string[] = [];
  let o = 0;
  while (o + 8 <= buf.length) {
    const b = readBox(buf, o);
    types.push(b.type);
    o += b.size;
  }
  return types;
}

test("parseAnnexB splits NALs and strips start codes", () => {
  const units = parseAnnexB(AU);
  assert.equal(units.length, 3);
  assert.deepEqual([...units[0]!], SPS);
  assert.deepEqual([...units[1]!], PPS);
  assert.deepEqual([...units[2]!], IDR);
});

test("parseAnnexB handles 3-byte start codes", () => {
  const buf = new Uint8Array([0, 0, 1, ...SPS, 0, 0, 1, ...IDR]);
  const units = parseAnnexB(buf);
  assert.equal(units.length, 2);
  assert.deepEqual([...units[0]!], SPS);
  assert.deepEqual([...units[1]!], IDR);
});

test("extractParamSets finds SPS and PPS", () => {
  const { sps, pps } = extractParamSets(parseAnnexB(AU));
  assert.deepEqual([...sps!], SPS);
  assert.deepEqual([...pps!], PPS);
});

test("annexBToAvcc keeps VCL, drops SPS/PPS, length-prefixes", () => {
  const avcc = annexBToAvcc(parseAnnexB(AU));
  // Only the IDR survives: [u32 len][IDR bytes].
  assert.deepEqual([...avcc], [0, 0, 0, IDR.length, ...IDR]);
});

test("buildInit is ftyp + moov and embeds SPS/PPS", () => {
  const init = buildInit(new Uint8Array(SPS), new Uint8Array(PPS), 1280, 720);
  assert.deepEqual(topLevelTypes(init), ["ftyp", "moov"]);
  // Top-level box sizes must exactly cover the buffer.
  assert.equal(topLevelTypes(init).length, 2);
  // SPS and PPS bytes appear inside (avcC box).
  const hay = [...init].join(",");
  assert.ok(hay.includes(SPS.join(",")), "SPS embedded");
  assert.ok(hay.includes(PPS.join(",")), "PPS embedded");
});

test("buildSegment is moof + mdat with correct data_offset and mdat payload", () => {
  const sample = annexBToAvcc(parseAnnexB(AU));
  const seg = buildSegment({
    sample,
    baseMediaDecodeTime: 0,
    duration: 33333,
    keyframe: true,
    sequenceNumber: 1,
  });
  assert.deepEqual(topLevelTypes(seg), ["moof", "mdat"]);

  const moof = readBox(seg, 0);
  const mdat = readBox(seg, moof.size);
  // mdat payload equals the sample bytes.
  assert.deepEqual([...seg.subarray(mdat.body, mdat.body + sample.length)], [...sample]);

  // trun.data_offset must point at the mdat payload (moof size + 8-byte mdat header).
  // Locate trun: it's the last box in traf; find "trun" and read the field 16 bytes in.
  const trunIdx = [...seg].findIndex(
    (_, i) => seg[i] === 0x74 && seg[i + 1] === 0x72 && seg[i + 2] === 0x75 && seg[i + 3] === 0x6e,
  );
  const off = trunIdx - 4 + 16; // box start = type - 4; data_offset at +16
  const dataOffset = (seg[off]! << 24) | (seg[off + 1]! << 16) | (seg[off + 2]! << 8) | seg[off + 3]!;
  assert.equal(dataOffset, moof.size + 8);
});
