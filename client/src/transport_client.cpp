#include "transport_client.h"
#include "client_socket_channel.h"
#include "tls_trust.h"
#include "pairing_code.h"
#include <chrono>

namespace droppix {

std::optional<PairingProbe> probe_pairing_code(const std::string& host, uint16_t port,
                                               int timeout_ms) {
  auto ch = ClientSocketChannel::connect(host, port, /*use_tls=*/true, timeout_ms);
  if (!ch || !ch->peer_certificate()) return std::nullopt;
  auto der = cert_der(ch->peer_certificate());
  std::string fp = cert_fingerprint(ch->peer_certificate());
  ch->close();
  if (der.empty() || fp.empty()) return std::nullopt;
  return PairingProbe{derive_pairing_code(der), fp};
}

namespace {
std::vector<unsigned char> u64_be(uint64_t x) {
  std::vector<unsigned char> b(8);
  for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>(x >> (56 - i * 8));
  return b;
}
int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

void TransportClient::close() {
  std::lock_guard<std::mutex> lk(sendLock_);
  channel_ = nullptr;
}

void TransportClient::sendTouch(const std::vector<TouchContact>& contacts) {
  std::lock_guard<std::mutex> lk(sendLock_);
  if (!channel_) return;
  auto msg = encode_message(MsgType::Touch, encode_touch(contacts));
  channel_->send_all(msg.data(), msg.size());
}

void TransportClient::sendOrientation(uint8_t code) {
  std::lock_guard<std::mutex> lk(sendLock_);
  if (!channel_) return;
  auto msg = encode_message(MsgType::Orientation, encode_orientation(code));
  channel_->send_all(msg.data(), msg.size());
}

void TransportClient::runOverChannel(ByteChannel& channel, uint32_t width, uint32_t height,
                                    uint32_t density, StreamListener& listener,
                                    const std::function<bool()>& isRunning,
                                    const std::string& name, const std::string& id,
                                    int pingIntervalMs) {
  {
    std::lock_guard<std::mutex> lk(sendLock_);
    channel_ = &channel;
    auto hello = encode_message(MsgType::Hello,
        encode_hello(kProtocolVersion, width, height, density, name, id));
    if (!channel.send_all(hello.data(), hello.size())) { channel_ = nullptr; return; }
  }

  MessageParser parser;
  std::vector<unsigned char> chunk(65536);
  int64_t lastPing = 0;

  while (isRunning()) {
    int64_t nowMs = now_ms();
    if (nowMs - lastPing >= pingIntervalMs) {
      std::lock_guard<std::mutex> lk(sendLock_);
      if (!channel_) break;
      auto ping = encode_message(MsgType::Ping, u64_be(static_cast<uint64_t>(now_ns())));
      if (!channel.send_all(ping.data(), ping.size())) break;
      lastPing = nowMs;
    }
    if (!channel.wait_readable(200)) continue;   // periodic wakeup to recheck isRunning()
    ssize_t n = channel.recv(chunk.data(), chunk.size());
    if (n == 0) continue;      // no data ready (non-blocking edge) — keep looping
    if (n < 0) break;          // peer closed / error
    parser.feed(chunk.data(), static_cast<size_t>(n));
    ParsedMessage msg;
    while (parser.next(msg)) {
      switch (msg.type) {
        case MsgType::Config: {
          uint32_t w, h, fps; std::vector<unsigned char> ed;
          if (decode_config(msg.body, w, h, fps, ed)) listener.onConfig(w, h, fps, ed);
          break;
        }
        case MsgType::Video: {
          uint64_t pts; bool key; std::vector<unsigned char> nal;
          if (decode_video(msg.body, pts, key, nal)) listener.onVideo(pts, key, nal);
          break;
        }
        case MsgType::Audio:
          listener.onAudio(msg.body);
          break;
        case MsgType::Overlay: {
          uint8_t show;
          if (decode_overlay(msg.body, show)) listener.onOverlay(show != 0);
          break;
        }
        case MsgType::Ping: {
          std::lock_guard<std::mutex> lk(sendLock_);
          if (!channel_) break;
          auto pong = encode_message(MsgType::Pong, msg.body);
          channel.send_all(pong.data(), pong.size());
          break;
        }
        case MsgType::Pong:
        case MsgType::Bye:
          if (msg.type == MsgType::Bye) { std::lock_guard<std::mutex> lk(sendLock_); channel_ = nullptr; return; }
          break;
        default:
          break;  // ignore
      }
    }
  }
  std::lock_guard<std::mutex> lk(sendLock_);
  channel_ = nullptr;
}

}  // namespace droppix
