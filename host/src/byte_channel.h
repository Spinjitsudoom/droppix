#pragma once
#include <cstddef>
#include <sys/types.h>  // ssize_t

namespace droppix {

// A connected, bidirectional byte stream that TransportServer runs its framing/protocol
// over. Implementations: SocketChannel (TCP + optional TLS); AoaChannel (USB bulk, M2).
struct ByteChannel {
  virtual ~ByteChannel() = default;
  // Read up to n bytes; returns >0 bytes read, or <=0 on close/error.
  virtual ssize_t recv(void* buf, size_t n) = 0;
  // Write all n bytes; returns true iff every byte was written.
  virtual bool send_all(const unsigned char* p, size_t n) = 0;
  // True iff data is readable within timeout_ms (0 = poll now). MUST also return true
  // when the implementation holds already-buffered readable bytes (e.g. TLS decrypted).
  virtual bool wait_readable(int timeout_ms) = 0;
  virtual bool connected() const = 0;
  virtual void close() = 0;
  // Bytes handed to the kernel but not yet acknowledged by the peer (SIOCOUTQ).
  //
  // This is the ONLY reliable "is the client keeping up?" signal on a blocking socket:
  // send_all() returns instantly while the kernel buffer has room, so a client on a link
  // slower than the encoder never shows up as a slow write — the excess silently piles
  // into buffers, latency grows without bound, and the stream degrades while the host
  // still measures a healthy send time. A growing backlog here is what exposes that.
  //
  // Returns 0 when unsupported (non-socket transports), which reads as "no backlog" and
  // leaves callers on their normal path.
  virtual size_t pending_bytes() const { return 0; }
};

}  // namespace droppix
