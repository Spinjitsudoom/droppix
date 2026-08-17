#pragma once
#include <utility>
namespace droppix {
// ORIENTATION wire codes: 0=0°, 1=90°, 2=180°, 3=270°. The host only distinguishes
// PORTRAIT (90/270) from landscape (0/180): it streams portrait- vs landscape-shaped
// dimensions accordingly. The tablet's Android auto-rotate handles the up/down flips
// (0<->180, 90<->270) visually, so the host need not rotate at all.
inline bool orientation_is_portrait(int code) { return code == 1 || code == 3; }

// Force (w,h) into the shape the reported orientation implies.
//
// The client's HELLO dimensions and its ORIENTATION can disagree, and the orientation is the
// authoritative one: the tablet knows how it is being held, while its dimensions are whatever
// the app happened to measure when the session started. A portrait-natural phone
// (`configChanges="orientation|screenSize"`, so no Activity restart on rotation) keeps sending
// the dimensions it measured at launch no matter how it is turned.
//
// This must normalise BOTH ways. It used to only swap landscape dims to portrait, on the
// assumption — true of the old landscape-natural tablet, false of a phone — that the client
// "always sends landscape HELLO dims". Portrait dims plus a landscape orientation therefore
// fell through unchanged, which built a portrait monitor for a landscape device (the picture
// became a narrow strip between black bars) AND never settled: the orientation handler
// restarts the session whenever the reported shape differs from the built one, the reconnect
// re-sent the same unswapped dims, and the session restarted every few seconds forever.
inline void orient_dims(int code, int& w, int& h) {
  const bool want_portrait = orientation_is_portrait(code);
  if (want_portrait == (h > w)) return;   // already the right shape (square: nothing to do)
  std::swap(w, h);
}
}  // namespace droppix
