#pragma once
#include <string>

namespace droppix {

// Web-path pairing message types (WSS only; NOT part of the shared MsgType enum the
// native Android/Qt clients use). The web client sends kMsgPair with the 6 ASCII
// digits; the host replies kMsgPairResult with [ok u8][tries_left u8].
constexpr unsigned char kMsgPair = 20;
constexpr unsigned char kMsgPairResult = 21;
constexpr int kWebPinMaxTries = 5;

// Constant-length compare of the submitted PIN against the host's pairing code.
// (Both are the 6-digit derive_pairing_code() output; length mismatch => false.)
inline bool pin_matches(const std::string& submitted, const std::string& expected) {
  if (submitted.size() != expected.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < expected.size(); ++i)
    diff |= static_cast<unsigned char>(submitted[i] ^ expected[i]);
  return diff == 0;
}

}  // namespace droppix
