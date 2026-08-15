#pragma once
#include <atomic>
#include <csignal>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "frame_source.h"

namespace droppix {

// A spacedesk-compatible display server: lets the proprietary spacedesk VIEWER app
// connect to droppix as if droppix were a spacedesk host.
//
// Protocol reverse-engineered from a real server; see
// docs/superpowers/specs/2026-08-15-spacedesk-protocol-notes.md and spacedesk_protocol.h.
// It answers the viewer's UDP discovery broadcast, accepts a TCP session, and streams the
// display as baseline JPEG stripes.
//
// SECURITY: the spacedesk protocol has no authentication of any kind — no TLS, no pairing,
// no approval step. Any device on the network that speaks it and connects will receive the
// screen. That is a deliberately different trust model from droppix's own transport (TLS +
// host-verified PIN + approval gate), and it is inherent to being wire-compatible: the
// viewer app cannot be asked for a credential it never sends. Run it only on networks you
// trust.
class SpacedeskServer {
 public:
  // make_source(w, h) builds the display to serve, sized to the viewer's own screen (the
  // viewer reports it in its hello). Same shape as StreamDaemon's factory so callers can
  // hand over an evdi source, or a synthetic one in tests.
  using SourceFactory = std::function<std::unique_ptr<FrameSource>(int w, int h)>;

  SpacedeskServer(SourceFactory make_source, std::string machine_name, int jpeg_quality = 75);
  ~SpacedeskServer();

  // Starts the discovery responder and the TCP listener on a background thread.
  // Returns false if the port could not be bound (e.g. a real spacedesk server, or a
  // second droppix, already holds it).
  bool start();
  void stop();
  bool running() const { return running_.load(); }

  // Diagnostics for the GUI/log.
  uint64_t frames_sent() const { return frames_sent_.load(); }
  bool client_connected() const { return client_connected_.load(); }

  // Bound port (kPort unless overridden for tests).
  void set_port(uint16_t p) { port_ = p; }

 private:
  void run();                       // accept loop
  void serve_client(int fd);        // one viewer session
  void discovery_loop();            // UDP responder

  SourceFactory make_source_;
  std::string machine_name_;
  int jpeg_quality_;
  uint16_t port_;

  int listen_fd_ = -1;
  int udp_fd_ = -1;
  std::thread accept_thread_;
  std::thread discovery_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<uint64_t> frames_sent_{0};
  std::atomic<bool> client_connected_{false};
};

}  // namespace droppix
