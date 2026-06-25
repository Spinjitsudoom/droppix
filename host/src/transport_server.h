#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "protocol.h"

namespace droppix {
class TransportServer {
 public:
  ~TransportServer();
  bool listen(uint16_t port);          // 0 = ephemeral
  uint16_t port() const { return port_; }
  bool accept_client(int timeout_ms);
  bool read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                  int timeout_ms);
  bool send_config(uint32_t w, uint32_t h, uint32_t fps,
                   const std::vector<unsigned char>& extradata);
  bool send_video(uint64_t pts_us, bool keyframe,
                  const std::vector<unsigned char>& nal);
  void poll_control();                 // respond to PING, dispatch INPUT, detect disconnect
  // Called for each INPUT message during poll_control (action, x_norm, y_norm).
  // INVARIANT: if the handler captures an object by reference, the caller MUST
  // clear it (set_input_handler(nullptr)) before that object is destroyed —
  // TransportServer outlives a single streaming session.
  void set_input_handler(std::function<void(uint8_t, uint16_t, uint16_t)> h) {
    input_handler_ = std::move(h);
  }
  bool connected() const { return client_fd_ >= 0; }
  void close_all();

 private:
  bool send_all(const std::vector<unsigned char>& bytes);
  bool wait_readable(int fd, int timeout_ms);

  int listen_fd_ = -1;
  int client_fd_ = -1;
  uint16_t port_ = 0;
  MessageParser parser_;
  std::function<void(uint8_t, uint16_t, uint16_t)> input_handler_;
};
}  // namespace droppix
