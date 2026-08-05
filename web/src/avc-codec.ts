/**
 * Derive the WebCodecs `avc1.PPCCLL` codec string from an H.264 Annex-B access
 * unit by reading the SPS's profile_idc / constraint flags / level_idc.
 *
 * The web decoder must declare a profile/level >= the actual stream, or Chrome
 * refuses to decode (black screen). droppix's encoders don't cap the level —
 * x264 emits Level 4.0 at 1080p, NVENC/VAAPI default to High profile — so a
 * hardcoded string (`avc1.42E01F` = Constrained Baseline 3.1) mismatches real
 * streams. Parsing the SPS makes the web client decode whatever the host sends,
 * exactly like the Android client (which configures MediaCodec from the SPS).
 */
export function avcCodecString(nal: Uint8Array): string | null {
  const n = nal.length;
  // Walk Annex-B start codes (00 00 01, optionally 00-prefixed => 00 00 00 01).
  for (let i = 0; i + 3 < n; i++) {
    if (nal[i] === 0 && nal[i + 1] === 0 && nal[i + 2] === 1) {
      const header = i + 3; // NAL header byte
      if ((nal[header]! & 0x1f) === 7) {
        // SPS: header is followed by profile_idc, constraint_set flags, level_idc.
        if (header + 3 >= n) return null; // truncated SPS
        const profile = nal[header + 1]!;
        const constraints = nal[header + 2]!;
        const level = nal[header + 3]!;
        const hex = (b: number) => b.toString(16).toUpperCase().padStart(2, "0");
        return `avc1.${hex(profile)}${hex(constraints)}${hex(level)}`;
      }
    }
  }
  return null;
}
