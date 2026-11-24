#pragma once
#include <cstddef>
#include <sys/ioctl.h>
#include <linux/sockios.h>  // SIOCOUTQ
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace droppix {

// Bytes sitting in the socket's send queue (written by us, not yet ACKed by the peer).
// Shared by the TCP and WebSocket channels — both wrap a socket fd. Returns 0 on any
// failure so callers treat "unknown" as "no backlog".
inline size_t socket_pending_bytes(int fd) {
  if (fd < 0) return 0;
  int queued = 0;
  if (::ioctl(fd, SIOCOUTQ, &queued) != 0 || queued < 0) return 0;
  return static_cast<size_t>(queued);
}

// Dead-peer detection for an accepted client socket. A tablet that drops off Wi-Fi sends no
// FIN; without this the kernel retried for ~15 minutes, the stream loop just paused on the
// send backlog, and the tablet could not reconnect (the one-client listener is not polled
// while a session runs). Keep-alives catch an idle peer (5 s idle + 3 probes 2 s apart) and
// TCP_USER_TIMEOUT fails a send that stays unacknowledged for 10 s.
constexpr int kKeepIdleS = 5, kKeepIntvlS = 2, kKeepCnt = 3, kUserTimeoutMs = 10000;
inline void enable_dead_peer_detection(int fd) {
  const int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
  ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &kKeepIdleS, sizeof kKeepIdleS);
  ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &kKeepIntvlS, sizeof kKeepIntvlS);
  ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &kKeepCnt, sizeof kKeepCnt);
  ::setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &kUserTimeoutMs, sizeof kUserTimeoutMs);
}

}  // namespace droppix
