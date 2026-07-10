#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "byte_channel.h"
#include "protocol.h"

namespace droppix {

// Result of a pairing probe: the human-facing 6-digit code AND the full fingerprint to
// actually pin (the code is a lossy digest of it, not itself suitable as the stored pin).
struct PairingProbe {
  std::string code;
  std::string fingerprint;
};

// Opens a throwaway TLS connection purely to capture the host's certificate, derive the
// 6-digit pairing code from it, and compute its fingerprint (mirrors
// ConnectActivity.kt's pairThenConnect probe). Returns nullopt on any connect/handshake
// failure.
std::optional<PairingProbe> probe_pairing_code(const std::string& host, uint16_t port,
                                               int timeout_ms = 5000);

// Thrown by TransportClient::run() — mirrors the two terminal exceptions
// StreamActivity.kt catches specially (net/TransportClient.kt run(), StreamActivity.kt:216-226).
struct NotPairedException : std::runtime_error {
  NotPairedException() : std::runtime_error("not paired") {}
};
struct CertChangedException : std::runtime_error {
  explicit CertChangedException(const std::string& host)
      : std::runtime_error("PC identity changed for " + host) {}
};

// Callbacks for the transport-agnostic protocol loop (mirrors android/.../StreamListener).
struct StreamListener {
  virtual ~StreamListener() = default;
  virtual void onConfig(uint32_t width, uint32_t height, uint32_t fps,
                        const std::vector<unsigned char>& extradata) = 0;
  virtual void onVideo(uint64_t pts_us, bool keyframe, const std::vector<unsigned char>& nal) = 0;
  virtual void onAudio(const std::vector<unsigned char>& pcm) {}
  virtual void onOverlay(bool show) {}
};

// C++ port of android/.../net/TransportClient.kt. One instance per stream session.
// sendTouch/sendOrientation may be called from a different thread than run() (e.g. the
// Qt UI thread for input vs. a dedicated net thread for the blocking read loop) — writes
// are serialized with a mutex against the loop's own PING/PONG-echo writes, same reason
// the Android app does it (NetworkOnMainThreadException avoidance there; here it's just
// making sure two threads never interleave partial writes on the same channel).
class TransportClient {
 public:
  void close();  // marks the channel not to be written to further; run() must still be joined

  void sendTouch(const std::vector<TouchContact>& contacts);
  void sendOrientation(uint8_t code);

  // Transport-agnostic loop: writes HELLO once, then reads CONFIG/VIDEO/AUDIO/OVERLAY/PING
  // and answers PING while isRunning() holds. Returns when the channel closes or isRunning()
  // goes false. Caller owns `channel`'s lifetime and connects it beforehand.
  void runOverChannel(ByteChannel& channel, uint32_t width, uint32_t height, uint32_t density,
                      StreamListener& listener, const std::function<bool()>& isRunning,
                      const std::string& name, const std::string& id,
                      int pingIntervalMs = 1000);

 private:
  std::mutex sendLock_;
  ByteChannel* channel_ = nullptr;  // valid only during runOverChannel
};

}  // namespace droppix
