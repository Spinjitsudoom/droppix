#pragma once
#include <cstdint>
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
  void poll_control();                 // respond to PING, detect disconnect
  bool connected() const { return client_fd_ >= 0; }
  void close_all();

 private:
  bool send_all(const std::vector<unsigned char>& bytes);
  bool wait_readable(int fd, int timeout_ms);

  int listen_fd_ = -1;
  int client_fd_ = -1;
  uint16_t port_ = 0;
  MessageParser parser_;
};
}  // namespace droppix
