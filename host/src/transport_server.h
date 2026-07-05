#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <openssl/ssl.h>
#include "byte_channel.h"
#include "protocol.h"

namespace droppix {
class TransportServer {
 public:
  ~TransportServer();
  bool listen(uint16_t port);          // 0 = ephemeral
  uint16_t port() const { return port_; }
  bool accept_client(int timeout_ms);
  // Adopt an already-connected byte stream (e.g. an AOA USB channel) instead of accepting
  // a TCP client. accept_client() uses this internally with a SocketChannel.
  void adopt_channel(std::unique_ptr<ByteChannel> ch, std::string peer = "");
  bool read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                  std::string& name, std::string& id, int timeout_ms);
  bool send_config(uint32_t w, uint32_t h, uint32_t fps,
                   const std::vector<unsigned char>& extradata);
  bool send_video(uint64_t pts_us, bool keyframe,
                  const std::vector<unsigned char>& nal);
  bool send_audio(const std::vector<unsigned char>& pcm);
  bool send_overlay(uint8_t show);   // tell the app to show/hide the perf overlay
  void poll_control();                 // respond to PING, dispatch touch, detect disconnect
  // Called for each TOUCH message during poll_control with the full active-contact set (a
  // legacy single-pointer INPUT is delivered as a 1-contact set, or empty on release).
  // INVARIANT: if the handler captures an object by reference, the caller MUST clear it
  // (set_touch_handler(nullptr)) before that object is destroyed — TransportServer outlives
  // a single streaming session.
  void set_touch_handler(std::function<void(const std::vector<TouchContact>&)> h) {
    touch_handler_ = std::move(h);
  }
  // Called for each ORIENTATION message during poll_control (code: 0/1/2/3 =>
  // 0/90/180/270). Same lifetime invariant as the input handler.
  void set_orientation_handler(std::function<void(uint8_t)> h) {
    orientation_handler_ = std::move(h);
  }
  bool connected() const { return channel_ && channel_->connected(); }
  std::string peer_ip() const { return peer_ip_; }
  void close_all();
  // Enables TLS for all subsequent accepted clients. Call BEFORE accept_client.
  // Sets up a process-lifetime SSL_CTX from the given cert/key (PEM files).
  void enable_tls(const std::string& cert_path, const std::string& key_path);

 private:
  bool send_all(const std::vector<unsigned char>& bytes);

  int listen_fd_ = -1;
  uint16_t port_ = 0;
  std::string peer_ip_;
  MessageParser parser_;
  std::function<void(const std::vector<TouchContact>&)> touch_handler_;
  std::function<void(uint8_t)> orientation_handler_;

  bool tls_ = false;
  std::string cert_, key_;
  SSL_CTX* ctx_ = nullptr;
  std::unique_ptr<ByteChannel> channel_;   // the live client connection (socket or AOA)
};
}  // namespace droppix
