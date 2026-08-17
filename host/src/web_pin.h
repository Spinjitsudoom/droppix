#pragma once
#include <cstddef>
#include <string>

namespace droppix {

// Web-path pairing message types (WSS only; NOT part of the shared MsgType enum the
// native Android/Qt clients use). The web client sends kMsgPair with the 6 ASCII
// digits; the host replies kMsgPairResult with [ok u8][tries_left u8].
constexpr unsigned char kMsgPair = 20;
constexpr unsigned char kMsgPairResult = 21;
constexpr int kWebPinMaxTries = 5;

// Web client settings, persisted by the HOST (WSS-only, like the pair messages above).
//
// The browser's own localStorage is not durable enough for this: it is scoped to the exact
// origin (scheme + host + PORT, and droppix's session port moves), and browsers decline to
// persist it reliably for an origin whose certificate the user had to click through. So
// settings vanished between sessions even though the client stored them correctly.
//
// The host keeps the blob instead and hands it back once the client has proved it knows the
// pairing code — the same gate that guards the stream, so this adds no new trust boundary
// and no unauthenticated write endpoint. Body is the client's settings JSON, UTF-8.
constexpr unsigned char kMsgClientSettings = 22;

// Refuse anything larger; the blob is a small flat JSON object and this is a network input.
constexpr size_t kMaxClientSettingsBytes = 8192;

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
