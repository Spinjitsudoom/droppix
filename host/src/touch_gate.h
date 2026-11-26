#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "protocol.h"     // TouchContact
#include "tap_gesture.h"  // TwoFingerTap thresholds

namespace droppix {

// Holds a new touch back from the desktop until it is clearly not a two-finger tap.
//
// A two-finger tap is a right-click, but its fingers used to reach the desktop as ordinary
// touches too. Qt turns the first touch into a mouse press and releases it when the touch ends
// (and, by design, when the touch is cancelled), so the tap ALSO clicked whatever was under the
// first finger: a button, a link, a file. The only way to click nothing is to never deliver it.
//
// So a new touch is held:
//  - lifted while held: a single tap is delivered whole at that moment (clicks happen on the
//    release anyway, so a tap feels no later); a two-finger tap is swallowed;
//  - moved past the tap threshold: released at once, starting from where it went down so a
//    drag grabs the right thing;
//  - held still: released after kHoldMs (one finger) or the tap time (two fingers).
// Pure, no I/O; tick() lets a held touch expire without a new event.
class TouchGate {
 public:
  using Frame = std::vector<TouchContact>;   // a full active-contact set; empty = all lifted
  static constexpr int64_t kHoldMs = 100;

  // Frames to deliver now, in order.
  std::vector<Frame> update(const Frame& contacts, int64_t now_ms);
  std::vector<Frame> tick(int64_t now_ms);
  void reset() { state_ = State::Idle; first_.clear(); last_.clear(); initPos_.clear(); }

 private:
  enum class State { Idle, Holding, Passing };
  bool expired(int64_t now_ms) const {
    return now_ms - start_ > (maxCount_ >= 2 ? TwoFingerTap::kTapMs : kHoldMs);
  }
  std::vector<Frame> release();   // Holding -> Passing: the first frame, every finger at its start, then the latest

  State state_ = State::Idle;
  int64_t start_ = 0;
  int maxCount_ = 0;
  bool moved_ = false;
  Frame first_, last_;
  std::map<uint8_t, std::pair<uint16_t, uint16_t>> initPos_;
};

}  // namespace droppix
