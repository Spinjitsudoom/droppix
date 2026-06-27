#pragma once
namespace droppix {
// ORIENTATION wire codes: 0=0°, 1=90°, 2=180°, 3=270°. The host only distinguishes
// PORTRAIT (90/270) from landscape (0/180): it streams portrait- vs landscape-shaped
// dimensions accordingly. The tablet's Android auto-rotate handles the up/down flips
// (0<->180, 90<->270) visually, so the host need not rotate at all.
inline bool orientation_is_portrait(int code) { return code == 1 || code == 3; }
}  // namespace droppix
