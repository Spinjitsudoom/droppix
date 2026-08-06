/**
 * Minimal H.264 Annex-B → fragmented-MP4 muxer for Media Source Extensions.
 *
 * The host sends raw H.264 access units (Annex-B NALs, SPS/PPS in-band ahead of
 * each IDR). MSE wants fMP4: one `init` segment (ftyp+moov, carrying SPS/PPS in an
 * avcC box) followed by `media` segments (moof+mdat with length-prefixed AVCC NALs).
 * This builds both with no dependencies so a `<video>` element can decode/render
 * natively — much cheaper than WebCodecs+canvas on low-end GPUs.
 *
 * Timescale is microseconds (1e6) so stream PTS maps 1:1 to media time (tfdt is
 * 64-bit, so large values are fine). Pure functions only — unit-tested by byte layout.
 */

const TIMESCALE = 1_000_000;

// ---- byte helpers ----------------------------------------------------------

function u16(v: number): number[] {
  return [(v >>> 8) & 0xff, v & 0xff];
}
function u32(v: number): number[] {
  return [(v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff];
}
function u64(v: number): number[] {
  // v may exceed 32 bits; split via division to stay exact past 2^32.
  const hi = Math.floor(v / 0x1_0000_0000);
  const lo = v >>> 0;
  return [...u32(hi), ...u32(lo)];
}
function s4(s: string): number[] {
  return [s.charCodeAt(0), s.charCodeAt(1), s.charCodeAt(2), s.charCodeAt(3)];
}
function concat(parts: Uint8Array[]): Uint8Array {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

/** `[size u32][type][payload…]`. */
function box(type: string, payload: Uint8Array): Uint8Array {
  const size = 8 + payload.length;
  return concat([new Uint8Array([...u32(size), ...s4(type)]), payload]);
}
/** Full box: adds the `[version][flags u24]` header before the payload. */
function fbox(type: string, version: number, flags: number, payload: Uint8Array): Uint8Array {
  const head = new Uint8Array([version & 0xff, (flags >>> 16) & 0xff, (flags >>> 8) & 0xff, flags & 0xff]);
  return box(type, concat([head, payload]));
}
function bytes(a: number[]): Uint8Array {
  return new Uint8Array(a);
}

// ---- Annex-B parsing -------------------------------------------------------

/** Split an Annex-B buffer into raw NAL units (start codes stripped). */
export function parseAnnexB(buf: Uint8Array): Uint8Array[] {
  const units: Uint8Array[] = [];
  const n = buf.length;
  let i = 0;
  // Find first start code.
  const isStart = (p: number) =>
    p + 2 < n && buf[p] === 0 && buf[p + 1] === 0 && buf[p + 2] === 1;
  while (i + 2 < n && !isStart(i)) i++;
  while (i + 2 < n) {
    const start = i + 3; // past 00 00 01
    let j = start;
    while (j + 2 < n && !(buf[j] === 0 && buf[j + 1] === 0 && buf[j + 2] === 1)) j++;
    let end = j + 2 < n ? j : n;
    // A 4-byte start code (00 00 00 01) leaves a trailing 0 at end-1 of this NAL.
    if (end > start && end < n && buf[end - 1] === 0) end--;
    if (end > start) units.push(buf.subarray(start, end));
    i = j;
  }
  return units;
}

const nalType = (u: Uint8Array): number => (u.length ? u[0]! & 0x1f : 0);

/** Extract SPS (type 7) and PPS (type 8) from a set of NAL units, if present. */
export function extractParamSets(units: Uint8Array[]): { sps?: Uint8Array; pps?: Uint8Array } {
  let sps: Uint8Array | undefined;
  let pps: Uint8Array | undefined;
  for (const u of units) {
    const t = nalType(u);
    if (t === 7 && !sps) sps = u;
    else if (t === 8 && !pps) pps = u;
  }
  return { sps, pps };
}

/** Concatenate VCL/SEI NALs as AVCC samples (`[len u32][nal]…`); SPS/PPS/AUD dropped. */
export function annexBToAvcc(units: Uint8Array[]): Uint8Array {
  const parts: Uint8Array[] = [];
  for (const u of units) {
    const t = nalType(u);
    if (t === 7 || t === 8 || t === 9) continue; // params + access-unit-delimiter live elsewhere
    parts.push(bytes(u32(u.length)));
    parts.push(u);
  }
  return concat(parts);
}

// ---- init segment (ftyp + moov) --------------------------------------------

function avcC(sps: Uint8Array, pps: Uint8Array): Uint8Array {
  // profile/compat/level come from the SPS header bytes (after the 1-byte NAL header).
  const payload = bytes([
    1, // configurationVersion
    sps[1]!, // AVCProfileIndication
    sps[2]!, // profile_compatibility
    sps[3]!, // AVCLevelIndication
    0xff, // 6 bits reserved + lengthSizeMinusOne = 3 (4-byte NAL length)
    0xe1, // 3 bits reserved + numOfSequenceParameterSets = 1
    ...u16(sps.length),
    ...sps,
    1, // numOfPictureParameterSets
    ...u16(pps.length),
    ...pps,
  ]);
  return box("avcC", payload);
}

function avc1(sps: Uint8Array, pps: Uint8Array, width: number, height: number): Uint8Array {
  const payload = concat([
    bytes([
      0, 0, 0, 0, 0, 0, // reserved (6)
      ...u16(1), // data_reference_index
      0, 0, // pre_defined
      0, 0, // reserved
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // pre_defined (3 x u32)
      ...u16(width),
      ...u16(height),
      0x00, 0x48, 0x00, 0x00, // horizresolution 72dpi
      0x00, 0x48, 0x00, 0x00, // vertresolution 72dpi
      0, 0, 0, 0, // reserved
      ...u16(1), // frame_count
      // compressorname: 32 bytes, length-prefixed pascal string (all zero here)
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0x00, 0x18, // depth
      0xff, 0xff, // pre_defined = -1
    ]),
    avcC(sps, pps),
  ]);
  return box("avc1", payload);
}

function moov(sps: Uint8Array, pps: Uint8Array, width: number, height: number): Uint8Array {
  const mvhd = fbox("mvhd", 0, 0, bytes([
    ...u32(0), ...u32(0), // creation, modification time
    ...u32(TIMESCALE),
    ...u32(0), // duration (fragmented => 0)
    0x00, 0x01, 0x00, 0x00, // rate 1.0
    0x01, 0x00, // volume 1.0
    0, 0, // reserved
    ...u32(0), ...u32(0), // reserved
    // unity matrix
    ...u32(0x00010000), ...u32(0), ...u32(0),
    ...u32(0), ...u32(0x00010000), ...u32(0),
    ...u32(0), ...u32(0), ...u32(0x40000000),
    ...u32(0), ...u32(0), ...u32(0), ...u32(0), ...u32(0), ...u32(0), // pre_defined
    ...u32(2), // next_track_ID
  ]));

  const tkhd = fbox("tkhd", 0, 0x000007, bytes([
    ...u32(0), ...u32(0), // times
    ...u32(1), // track_ID
    ...u32(0), // reserved
    ...u32(0), // duration
    ...u32(0), ...u32(0), // reserved
    0, 0, // layer
    0, 0, // alternate_group
    0, 0, // volume (0 for video)
    0, 0, // reserved
    ...u32(0x00010000), ...u32(0), ...u32(0),
    ...u32(0), ...u32(0x00010000), ...u32(0),
    ...u32(0), ...u32(0), ...u32(0x40000000),
    ...u32(width << 16), // width 16.16
    ...u32(height << 16), // height 16.16
  ]));

  const mdhd = fbox("mdhd", 0, 0, bytes([
    ...u32(0), ...u32(0),
    ...u32(TIMESCALE),
    ...u32(0), // duration
    0x55, 0xc4, // language 'und'
    0, 0, // pre_defined
  ]));
  const hdlr = fbox("hdlr", 0, 0, bytes([
    ...u32(0), // pre_defined
    ...s4("vide"), // handler_type
    ...u32(0), ...u32(0), ...u32(0), // reserved
    ...s4("drop"), 0x70, 0x69, 0x78, 0x00, // "droppix\0" name
  ]));

  const vmhd = fbox("vmhd", 0, 1, bytes([0, 0, 0, 0, 0, 0, 0, 0]));
  const dref = fbox("dref", 0, 0, concat([bytes(u32(1)), fbox("url ", 0, 1, new Uint8Array())]));
  const dinf = box("dinf", dref);

  const stsd = fbox("stsd", 0, 0, concat([bytes(u32(1)), avc1(sps, pps, width, height)]));
  const stts = fbox("stts", 0, 0, bytes(u32(0)));
  const stsc = fbox("stsc", 0, 0, bytes(u32(0)));
  const stsz = fbox("stsz", 0, 0, bytes([...u32(0), ...u32(0)]));
  const stco = fbox("stco", 0, 0, bytes(u32(0)));
  const stbl = box("stbl", concat([stsd, stts, stsc, stsz, stco]));

  const minf = box("minf", concat([vmhd, dinf, stbl]));
  const mdia = box("mdia", concat([mdhd, hdlr, minf]));
  const trak = box("trak", concat([tkhd, mdia]));

  const trex = fbox("trex", 0, 0, bytes([
    ...u32(1), // track_ID
    ...u32(1), // default_sample_description_index
    ...u32(0), // default_sample_duration
    ...u32(0), // default_sample_size
    ...u32(0), // default_sample_flags
  ]));
  const mvex = box("mvex", trex);

  return box("moov", concat([mvhd, trak, mvex]));
}

/** Build the fMP4 init segment (ftyp + moov) from SPS/PPS and display size. */
export function buildInit(sps: Uint8Array, pps: Uint8Array, width: number, height: number): Uint8Array {
  const ftyp = box("ftyp", bytes([
    ...s4("isom"),
    ...u32(1), // minor_version
    ...s4("isom"), ...s4("iso5"), ...s4("avc1"), ...s4("mp41"),
  ]));
  return concat([ftyp, moov(sps, pps, width, height)]);
}

// ---- media segment (moof + mdat) -------------------------------------------

const KEY_FLAGS = 0x0200_0000; // sample_depends_on = 2 (I-picture), sync sample
const NONKEY_FLAGS = 0x0101_0000; // depends_on = 1, sample_is_non_sync_sample = 1

/** Build one media segment for a single sample (access unit). */
export function buildSegment(opts: {
  sample: Uint8Array; // AVCC bytes from annexBToAvcc()
  baseMediaDecodeTime: number; // in TIMESCALE units (microseconds)
  duration: number; // in TIMESCALE units
  keyframe: boolean;
  sequenceNumber: number;
}): Uint8Array {
  const { sample, baseMediaDecodeTime, duration, keyframe, sequenceNumber } = opts;

  const mfhd = fbox("mfhd", 0, 0, bytes(u32(sequenceNumber)));

  // tfhd: default-base-is-moof (0x020000) so data offsets are relative to the moof.
  const tfhd = fbox("tfhd", 0, 0x020000, bytes(u32(1)));
  const tfdt = fbox("tfdt", 1, 0, bytes(u64(baseMediaDecodeTime)));

  // trun flags: data-offset(0x01) | first-sample-flags(0x04) | duration(0x100) | size(0x200)
  const trunFlags = 0x000001 | 0x000004 | 0x000100 | 0x000200;
  // data_offset points from the start of the moof to the first byte of mdat payload.
  // moof size = 8(moof) + mfhd + traf; traf = 8 + tfhd + tfdt + trun. trun holds the
  // data_offset field itself, so compute the total then patch it in.
  const trunPayloadNoOffset = concat([
    bytes(u32(1)), // sample_count
    bytes(u32(0)), // data_offset placeholder
    bytes(u32(keyframe ? KEY_FLAGS : NONKEY_FLAGS)), // first_sample_flags
    bytes(u32(duration)),
    bytes(u32(sample.length)),
  ]);
  const trun = fbox("trun", 0, trunFlags, trunPayloadNoOffset);
  const traf = box("traf", concat([tfhd, tfdt, trun]));
  const moof = box("moof", concat([mfhd, traf]));

  const dataOffset = moof.length + 8; // + mdat header
  // Patch data_offset: it sits right after sample_count in the trun payload. Locate it:
  // moof(8) mfhd tfhd tfdt trun(8 header + 4 version/flags... ) — easier to patch by offset.
  // trun box begins at: moof.length - trun.length. Its data_offset field is at
  // (trun start) + 8 (box header) + 4 (fullbox v/flags) + 4 (sample_count) = +16.
  const trunStart = moof.length - trun.length;
  const dataOffsetPos = trunStart + 16;
  moof[dataOffsetPos] = (dataOffset >>> 24) & 0xff;
  moof[dataOffsetPos + 1] = (dataOffset >>> 16) & 0xff;
  moof[dataOffsetPos + 2] = (dataOffset >>> 8) & 0xff;
  moof[dataOffsetPos + 3] = dataOffset & 0xff;

  const mdat = box("mdat", sample);
  return concat([moof, mdat]);
}

export const fmp4Timescale = TIMESCALE;
