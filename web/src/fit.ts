export type FitMode = "contain" | "cover" | "stretch";

export interface ContentBox {
  x: number;
  y: number;
  w: number;
  h: number;
}

/** Map video (vw,vh) into canvas CSS/pixel box (cw,ch) for the given fit mode. */
export function contentBox(cw: number, ch: number, vw: number, vh: number, mode: FitMode): ContentBox {
  if (cw <= 0 || ch <= 0 || vw <= 0 || vh <= 0) return { x: 0, y: 0, w: cw, h: ch };
  if (mode === "stretch") return { x: 0, y: 0, w: cw, h: ch };
  const scale =
    mode === "cover" ? Math.max(cw / vw, ch / vh) : Math.min(cw / vw, ch / vh);
  const w = vw * scale;
  const h = vh * scale;
  return { x: (cw - w) / 2, y: (ch - h) / 2, w, h };
}

/** Canvas-local pixel → 0..65535 in content space. null if outside content (contain letterbox). */
export function normalizePointer(
  px: number,
  py: number,
  box: ContentBox,
  outsideOk: boolean,
): { x: number; y: number } | null {
  const lx = px - box.x;
  const ly = py - box.y;
  if (!outsideOk && (lx < 0 || ly < 0 || lx > box.w || ly > box.h)) return null;
  const nx = Math.max(0, Math.min(1, box.w > 0 ? lx / box.w : 0));
  const ny = Math.max(0, Math.min(1, box.h > 0 ? ly / box.h : 0));
  return { x: Math.round(nx * 65535), y: Math.round(ny * 65535) };
}
