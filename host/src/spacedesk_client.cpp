#include "spacedesk_client.h"

#include "spacedesk_protocol.h"

namespace droppix::spacedesk {

void ClientParser::feed(const unsigned char* data, size_t n,
                        const std::function<void(const ServerMessage&)>& on_message) {
  buf_.insert(buf_.end(), data, data + n);
  for (;;) {
    if (buf_.size() < kHeaderSize) return;
    Header h;
    if (!parse_header(buf_.data(), buf_.size(), h)) return;
    // Guard against a desync turning a bogus length into a giant allocation.
    if (h.payload_len > 64u * 1024u * 1024u) {
      buf_.clear();
      return;
    }
    if (buf_.size() < kHeaderSize + h.payload_len) return;   // wait for the rest

    ServerMessage m;
    m.type = h.type;
    if (h.type == kMsgFrame) {
      const unsigned char* p = buf_.data();
      m.display_w = get_u32(p + 8);
      m.display_h = get_u32(p + 12);
      m.y_start = get_u32(p + 28);
      m.y_end = get_u32(p + 36);
      m.jpeg.assign(buf_.begin() + kHeaderSize,
                    buf_.begin() + kHeaderSize + h.payload_len);
    }
    buf_.erase(buf_.begin(),
               buf_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + h.payload_len));
    on_message(m);
  }
}

std::vector<unsigned char> build_client_hello(uint32_t width, uint32_t height,
                                              const std::string& guid,
                                              const std::string& model) {
  // The payload is a FIXED 334-byte block, not a variable-length string pair: GUID as
  // UTF-16LE at offset 0, the model at offset 78, zero-padded to the end. A shorter
  // variable-length payload is silently ignored by a real server — it answers nothing at
  // all — which is what the client probe caught.
  std::vector<unsigned char> payload(kHelloPayloadSize, 0);
  auto put_utf16 = [&payload](size_t at, const std::string& s) {
    for (size_t i = 0; i < s.size() && at + i * 2 + 1 < payload.size(); ++i) {
      payload[at + i * 2] = static_cast<unsigned char>(s[i]);
      payload[at + i * 2 + 1] = 0;
    }
  };
  put_utf16(0, guid);
  put_utf16(kHelloModelOffset, model);

  std::vector<unsigned char> m(kHeaderSize + payload.size(), 0);
  put_u32(m.data() + 0, kMsgHello);
  put_u32(m.data() + 4, static_cast<uint32_t>(payload.size()));
  // Header constants copied verbatim from the real viewer's hello, so the server treats
  // us as an ordinary client instead of negotiating something different.
  put_u32(m.data() + 8, 4);
  put_u32(m.data() + 12, 8);
  put_u32(m.data() + 20, 3);
  put_u32(m.data() + 24, 3);
  put_u32(m.data() + 28, 2);
  put_u32(m.data() + 32, 60);
  put_u32(m.data() + 36, 65541);
  put_u32(m.data() + 44, 262204);
  put_u32(m.data() + 48, 1);
  put_u32(m.data() + 52, width);    // our screen width
  put_u32(m.data() + 88, height);   // our screen height
  put_u32(m.data() + 124, 1);
  std::copy(payload.begin(), payload.end(), m.begin() + kHeaderSize);
  return m;
}

std::vector<unsigned char> build_client_keepalive(uint32_t seq) {
  std::vector<unsigned char> m(kHeaderSize, 0);
  put_u32(m.data() + 0, kMsgKeepalive);
  put_u32(m.data() + 8, 1220);   // constant seen in captured keepalives
  put_u32(m.data() + 24, seq);
  return m;
}

}  // namespace droppix::spacedesk
