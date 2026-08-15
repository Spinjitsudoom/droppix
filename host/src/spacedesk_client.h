#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace droppix::spacedesk {

// Client side of the spacedesk protocol: lets droppix consume a real spacedesk SERVER
// (the Windows driver) as a display source, the mirror image of SpacedeskServer.
//
// Together they give the four-way interop goal: droppix<->droppix over droppix's own
// protocol, droppix server<->spacedesk viewer, and spacedesk server<->droppix client.
//
// Parsing is kept separate from the socket so the wire format is unit-testable: feed()
// takes bytes from anywhere and emits decoded messages.
//
// NOTE: droppix's own transport (TLS + host-verified PIN + approval) does not apply here.
// spacedesk's protocol has no authentication, so a droppix client speaking it inherits
// that trust model.

// A decoded server->client message. `jpeg` is only populated for frames.
struct ServerMessage {
  uint32_t type = 0;
  uint32_t display_w = 0, display_h = 0;   // frames only
  uint32_t y_start = 0, y_end = 0;         // frames only: the stripe this covers
  std::vector<unsigned char> jpeg;         // frames only: a standalone baseline JFIF
};

// Incremental parser for the server's stream (128-byte header + optional payload).
class ClientParser {
 public:
  // Append raw bytes; complete messages are handed to `on_message` in order.
  void feed(const unsigned char* data, size_t n,
            const std::function<void(const ServerMessage&)>& on_message);
  // Bytes buffered awaiting a complete message (diagnostics).
  size_t buffered() const { return buf_.size(); }

 private:
  std::vector<unsigned char> buf_;
};

// The hello payload is a fixed-size block: GUID (UTF-16LE) at 0, model at 78, zero
// padded. Sizes taken from a real viewer's 462-byte hello (128 header + 334 payload).
inline constexpr size_t kHelloPayloadSize = 334;
inline constexpr size_t kHelloModelOffset = 78;

// The hello a droppix client sends to identify itself and declare its screen size.
// Mirrors the real viewer's message: type 0, size at @52/@88, UTF-16LE
// "{guid}\0MODEL" payload. The server sizes its virtual display from this.
std::vector<unsigned char> build_client_hello(uint32_t width, uint32_t height,
                                              const std::string& guid,
                                              const std::string& model);

// The keepalive a viewer sends while waiting; some servers expect traffic.
std::vector<unsigned char> build_client_keepalive(uint32_t seq);

}  // namespace droppix::spacedesk
