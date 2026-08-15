#pragma once
#include <cstddef>
#include <sys/ioctl.h>
#include <linux/sockios.h>  // SIOCOUTQ

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

}  // namespace droppix
