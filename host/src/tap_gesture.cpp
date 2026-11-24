#include "tap_gesture.h"
#include <cstdlib>

namespace droppix {

TwoFingerTap::Result TwoFingerTap::update(const std::vector<TouchContact>& contacts,
                                          int64_t now_ms) {
  if (!contacts.empty()) {
    if (!inGesture_) {                 // a new touch sequence begins
      inGesture_ = true;
      startMs_ = now_ms;
      maxCount_ = 0;
      moved_ = false;
      initPos_.clear();
      haveTwoCentroid_ = false;
    }
    if (static_cast<int>(contacts.size()) > maxCount_) maxCount_ = contacts.size();

    for (const auto& c : contacts) {
      auto it = initPos_.find(c.id);
      if (it == initPos_.end()) {
        initPos_[c.id] = {c.x, c.y};
      } else if (std::abs(c.x - it->second.first) > kMoveThresh ||
                 std::abs(c.y - it->second.second) > kMoveThresh) {
        moved_ = true;
      }
    }
    if (contacts.size() == 2) {
      centroidX_ = static_cast<uint16_t>((contacts[0].x + contacts[1].x) / 2);
      centroidY_ = static_cast<uint16_t>((contacts[0].y + contacts[1].y) / 2);
      haveTwoCentroid_ = true;
    }
    return {};
  }

  // All fingers up: decide whether the finished sequence was a two-finger tap.
  Result r;
  if (inGesture_) {
    if (maxCount_ == 2 && !moved_ && haveTwoCentroid_ && (now_ms - startMs_) <= kTapMs) {
      r = {true, centroidX_, centroidY_};
    }
    inGesture_ = false;
  }
  return r;
}

}  // namespace droppix
