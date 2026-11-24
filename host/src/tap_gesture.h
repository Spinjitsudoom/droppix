#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "protocol.h"   // TouchContact

namespace droppix {

// Detects a two-finger TAP in the touch-contact stream and reports it as a right-click.
// Pure/no I/O: fed each touch update (the full active-contact set + a monotonic time in ms).
// A right-click fires when all fingers lift and the gesture peaked at exactly two contacts,
// moved little, and was quick.
class TwoFingerTap {
 public:
  struct Result { bool rightClick = false; uint16_t x = 0; uint16_t y = 0; };
  Result update(const std::vector<TouchContact>& contacts, int64_t now_ms);

  // Tunable thresholds (0..65535 coord space; ms).
  static constexpr uint16_t kMoveThresh = 2500;   // ~4% of the screen
  static constexpr int64_t  kTapMs = 400;

 private:
  bool inGesture_ = false;
  int64_t startMs_ = 0;
  int maxCount_ = 0;
  bool moved_ = false;
  std::map<uint8_t, std::pair<uint16_t, uint16_t>> initPos_;   // id -> first position
  bool haveTwoCentroid_ = false;
  uint16_t centroidX_ = 0, centroidY_ = 0;   // midpoint while exactly two contacts were down
};

// Detects a one-finger PRESS-AND-HOLD and reports it as a right-click at the finger. Pure.
// Fed every contact update plus a periodic tick(): a finger held still sends no events, so
// the hold can only be noticed by the clock. Fires at most once per touch sequence (down
// until every finger is up), and only while exactly one finger has ever been down and it
// has stayed within kHoldSlop of where it landed.
class HoldGesture {
 public:
  struct Result { bool fire = false; uint8_t id = 0; uint16_t x = 0; uint16_t y = 0; };
  Result update(const std::vector<TouchContact>& contacts, int64_t now_ms);
  Result tick(int64_t now_ms);
  bool spent() const { return spent_; }

  static constexpr uint16_t kHoldSlop = 1000;   // ~1.5% of the output
  static constexpr int64_t  kHoldMs = 600;

 private:
  bool inGesture_ = false;
  bool disqualified_ = false;   // second finger, or moved beyond the slop
  bool spent_ = false;          // already fired this sequence
  int64_t startMs_ = 0;
  uint8_t id_ = 0;
  uint16_t x0_ = 0, y0_ = 0;    // where the finger landed
  uint16_t x_ = 0, y_ = 0;      // where it is now
};

}  // namespace droppix
