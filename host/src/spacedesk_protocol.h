#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// spacedesk VIEWER wire protocol (reverse-engineered — see
// docs/superpowers/specs/2026-08-15-spacedesk-protocol-notes.md).
//
// Lets the proprietary spacedesk viewer app connect to droppix. Everything here is
// pure byte-shuffling so it can be unit-tested without a socket.
//
// Discovery (UDP 28252): the viewer broadcasts a fixed magic; the server answers with a
// 308-byte struct carrying its name and TCP port.
//
// Session (TCP 28252): every message is a 128-byte header + optional payload, with the
// message type at @0 and the payload length at @4. Video frames (type 2) carry a plain
// baseline JPEG of a horizontal stripe of the display — the real server splits the screen
// into 4 stripes and sends one JPEG each.

namespace droppix::spacedesk {

inline constexpr uint16_t kPort = 28252;
inline constexpr size_t kHeaderSize = 128;
inline constexpr size_t kDiscoveryResponseSize = 308;
inline constexpr char kDiscoveryRequest[] = "SPACEDESK-NET-CLIENT";

// Message types observed on the wire.
enum : uint32_t {
  kMsgHello = 0,      // viewer -> server, payload = UTF-16LE "{guid}\0MODEL"
  kMsgHeartbeat = 1,  // server -> viewer
  kMsgFrame = 2,      // server -> viewer, payload = JPEG stripe
  kMsgDisplay = 3,    // server -> viewer
  kMsgAck = 4,        // server -> viewer, sent immediately after the hello
  kMsgKeepalive = 12, // viewer -> server while it waits
};

inline void put_u32(unsigned char* p, uint32_t v) {
  p[0] = static_cast<unsigned char>(v & 0xff);
  p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
  p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
  p[3] = static_cast<unsigned char>((v >> 24) & 0xff);
}

inline uint32_t get_u32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// True iff a datagram is the viewer's discovery broadcast. The viewer sends the magic
// with a trailing NUL; accept it with or without so a stray variant still resolves.
inline bool is_discovery_request(const unsigned char* data, size_t n) {
  const size_t magic_len = sizeof(kDiscoveryRequest) - 1;
  return n >= magic_len && std::memcmp(data, kDiscoveryRequest, magic_len) == 0;
}

// Build the 308-byte discovery response: machine name as UTF-16LE in a fixed buffer at
// @0, the TCP port at @260, then the constants the real server reports. The trailing
// values are version/capability fields; replicating the observed set keeps the viewer on
// its normal path (a stunted answer can make it negotiate differently).
inline std::vector<unsigned char> build_discovery_response(const std::string& machine_name,
                                                           uint16_t port = kPort) {
  std::vector<unsigned char> out(kDiscoveryResponseSize, 0);
  // ASCII -> UTF-16LE, truncated to the name buffer (260 bytes = 130 code units).
  const size_t max_chars = 260 / 2 - 1;
  const size_t n = machine_name.size() < max_chars ? machine_name.size() : max_chars;
  for (size_t i = 0; i < n; ++i) out[i * 2] = static_cast<unsigned char>(machine_name[i]);
  put_u32(out.data() + 260, port);
  put_u32(out.data() + 264, 8);
  put_u32(out.data() + 272, 4);
  put_u32(out.data() + 276, 8);
  put_u32(out.data() + 280, 43);
  return out;
}

// A parsed 128-byte header. Only the fields we actually use are named; the rest of the
// header is preserved verbatim when writing.
struct Header {
  uint32_t type = 0;
  uint32_t payload_len = 0;
};

inline bool parse_header(const unsigned char* p, size_t n, Header& out) {
  if (n < kHeaderSize) return false;
  out.type = get_u32(p);
  out.payload_len = get_u32(p + 4);
  return true;
}

// The viewer's hello carries the size of ITS screen — that is the geometry droppix should
// serve. Width sits at @52 and height at @88 (verified against a device whose panel is
// 2340x1080 and whose hello carried exactly those values).
struct ClientHello {
  uint32_t width = 0;
  uint32_t height = 0;
};

inline bool parse_client_hello(const unsigned char* p, size_t n, ClientHello& out) {
  Header h;
  if (!parse_header(p, n, h) || h.type != kMsgHello) return false;
  out.width = get_u32(p + 52);
  out.height = get_u32(p + 88);
  return out.width > 0 && out.height > 0;
}

// A header-only control message (ack / heartbeat / display).
inline std::vector<unsigned char> build_control(uint32_t type) {
  std::vector<unsigned char> h(kHeaderSize, 0);
  put_u32(h.data(), type);
  return h;
}

// One video stripe: header describing the rect, followed by its JPEG.
//
// Field layout copied from the real server's frames:
//   @8  full display width      @28 stripe y start
//   @12 full display height     @32 stripe x end (= full width)
//   @16 stride (width * 4)      @36 stripe y end
//   @68 payload length (again)
// The constants at @20/@40/@44/@48/@52/@60/@64 are reproduced as observed; their meaning
// is not established, but the viewer is known to accept exactly these.
inline std::vector<unsigned char> build_frame_header(uint32_t display_w, uint32_t display_h,
                                                     uint32_t y_start, uint32_t y_end,
                                                     uint32_t jpeg_len) {
  std::vector<unsigned char> h(kHeaderSize, 0);
  put_u32(h.data() + 0, kMsgFrame);
  put_u32(h.data() + 4, jpeg_len);
  put_u32(h.data() + 8, display_w);
  put_u32(h.data() + 12, display_h);
  put_u32(h.data() + 16, display_w * 4);   // 32bpp source stride
  put_u32(h.data() + 20, 21);
  put_u32(h.data() + 28, y_start);
  put_u32(h.data() + 32, display_w);
  put_u32(h.data() + 36, y_end);
  put_u32(h.data() + 40, 3);
  put_u32(h.data() + 44, 2);
  put_u32(h.data() + 48, 60);
  put_u32(h.data() + 52, 3);
  put_u32(h.data() + 60, 4);
  put_u32(h.data() + 64, 1);
  put_u32(h.data() + 68, jpeg_len);
  return h;
}

// The real server splits the display into this many horizontal stripes.
inline constexpr int kStripes = 4;

// Stripe bounds for index i, distributing any remainder into the last stripe so the
// stripes always tile the display exactly.
inline void stripe_bounds(uint32_t display_h, int i, uint32_t& y_start, uint32_t& y_end) {
  const uint32_t base = display_h / kStripes;
  y_start = base * static_cast<uint32_t>(i);
  y_end = (i == kStripes - 1) ? display_h : base * static_cast<uint32_t>(i + 1);
}

}  // namespace droppix::spacedesk
