#include "touch_gate.h"
#include <cstdlib>

namespace droppix {

std::vector<TouchGate::Frame> TouchGate::release() {
  state_ = State::Passing;
  // Every finger still down starts where it went down (a later finger was first seen in its
  // own event, after the first frame), then jumps to where it is now.
  Frame origin = last_;
  for (auto& c : origin) {
    const auto it = initPos_.find(c.id);
    if (it != initPos_.end()) { c.x = it->second.first; c.y = it->second.second; }
  }
  // The first frame matters only for a finger that has lifted since (it still touched down).
  for (const auto& c : first_) {
    bool stillDown = false;
    for (const auto& o : origin) stillDown = stillDown || o.id == c.id;
    if (!stillDown) return {first_, origin, last_};
  }
  return {origin, last_};
}

std::vector<TouchGate::Frame> TouchGate::update(const Frame& contacts, int64_t now_ms) {
  switch (state_) {
    case State::Passing:
      if (contacts.empty()) state_ = State::Idle;
      return {contacts};

    case State::Idle:
      if (contacts.empty()) return {};
      state_ = State::Holding;
      start_ = now_ms;
      maxCount_ = 0;
      moved_ = false;
      initPos_.clear();
      first_ = contacts;
      [[fallthrough]];

    case State::Holding: {
      if (contacts.empty()) {   // lifted while held
        state_ = State::Idle;
        const bool twoFingerTap = maxCount_ == 2 && !moved_ && now_ms - start_ <= TwoFingerTap::kTapMs;
        if (twoFingerTap) return {};   // the right-click is all it does
        return {first_, Frame{}};
      }
      if (static_cast<int>(contacts.size()) > maxCount_) maxCount_ = static_cast<int>(contacts.size());
      for (const auto& c : contacts) {
        auto it = initPos_.find(c.id);
        if (it == initPos_.end()) {
          initPos_[c.id] = {c.x, c.y};
        } else if (std::abs(c.x - it->second.first) > TwoFingerTap::kMoveThresh ||
                   std::abs(c.y - it->second.second) > TwoFingerTap::kMoveThresh) {
          moved_ = true;
        }
      }
      last_ = contacts;
      if (moved_ || maxCount_ > 2 || expired(now_ms)) return release();
      return {};
    }
  }
  return {};
}

std::vector<TouchGate::Frame> TouchGate::tick(int64_t now_ms) {
  if (state_ == State::Holding && expired(now_ms)) return release();
  return {};
}

}  // namespace droppix
