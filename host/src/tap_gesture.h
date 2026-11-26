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
  // cancelLifts: fingers lifting in this update belong to what is (so far) a two-finger tap, so
  // they must reach the desktop as a cancelled touch rather than a release. Qt turns a touch
  // into a mouse press, and releasing it clicked whatever was under that finger.
  struct Result { bool rightClick = false; uint16_t x = 0; uint16_t y = 0; bool cancelLifts = false; };
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
  bool tapSoFar(int64_t now_ms) const {
    return maxCount_ == 2 && !moved_ && haveTwoCentroid_ && (now_ms - startMs_) <= kTapMs;
  }
};

// A mouse-like right-click spread over time: land mid-tablet, then wiggle + move 15 ms later,
// press 30 ms after that, release 80 ms after that.
// - Land: Plasma 6's edge barrier holds a pointer back 100 px when it crosses onto another
//   screen, so jumping straight from the main screen to the target fell 100 px short (and a
//   target near the tablet's edge never left the main screen). Landing mid-tablet first takes
//   that loss on the tablet; the move to the target then stays on one screen.
// - Wiggle puts the pointer 1 px off: the kernel drops an ABS value equal to the last one
//   sent, so a repeat click on the same pixel would otherwise not move the pointer back.
// Pure; the injector polls due() from the stream loop instead of sleeping, so the video
// thread never blocks.
class ClickSchedule {
 public:
  enum class Step { Land, Wiggle, Move, Press, Release };
  void start(int64_t now_ms, uint16_t x, uint16_t y) {
    t0_ = now_ms; x_ = x; y_ = y; next_ = 0;
  }
  bool active() const { return next_ >= 0 && next_ < 5; }
  std::vector<Step> due(int64_t now_ms) {
    static constexpr int64_t kAt[5] = {0, 15, 15, 45, 125};
    std::vector<Step> out;
    while (active() && now_ms - t0_ >= kAt[next_]) out.push_back(static_cast<Step>(next_++));
    return out;
  }
  // Ends the click early (a new click, other pointer input, teardown). Returns Release when
  // the button is down, so it is never left held; a click that has not pressed yet is dropped.
  std::vector<Step> cancel() {
    const bool held = next_ == static_cast<int>(Step::Release);
    next_ = -1;
    return held ? std::vector<Step>{Step::Release} : std::vector<Step>{};
  }
  uint16_t x() const { return x_; }
  uint16_t y() const { return y_; }

 private:
  int64_t t0_ = 0;
  uint16_t x_ = 0, y_ = 0;
  int next_ = -1;   // -1 idle, 0..4 the next step, 5 done
};

}  // namespace droppix
