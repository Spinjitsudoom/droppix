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

HoldGesture::Result HoldGesture::update(const std::vector<TouchContact>& contacts,
                                        int64_t now_ms) {
  if (contacts.empty()) {   // sequence over: re-arm for the next one
    inGesture_ = disqualified_ = spent_ = false;
    return {};
  }
  if (!inGesture_) {
    inGesture_ = true;
    startMs_ = now_ms;
    id_ = contacts.front().id;
    x0_ = x_ = contacts.front().x;
    y0_ = y_ = contacts.front().y;
  }
  if (contacts.size() > 1 || contacts.front().id != id_) {
    disqualified_ = true;
    return {};
  }
  x_ = contacts.front().x;
  y_ = contacts.front().y;
  if (std::abs(int(x_) - int(x0_)) > kHoldSlop || std::abs(int(y_) - int(y0_)) > kHoldSlop)
    disqualified_ = true;
  return tick(now_ms);
}

HoldGesture::Result HoldGesture::tick(int64_t now_ms) {
  if (!inGesture_ || disqualified_ || spent_ || now_ms - startMs_ < kHoldMs) return {};
  spent_ = true;
  return {true, id_, x_, y_};
}

}  // namespace droppix
