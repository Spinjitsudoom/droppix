// Connect droppix's spacedesk CLIENT to a real spacedesk server and report what arrives.
//
// This is the acceptance check for the client direction: it uses the same
// build_client_hello / ClientParser the product uses, so a pass means droppix can consume
// a genuine spacedesk server — not that a throwaway script can.
//
//   spacedesk_probe <host[:port]> [seconds]
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "spacedesk_client.h"
#include "spacedesk_protocol.h"

using namespace droppix::spacedesk;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: spacedesk_probe <host[:port]> [seconds]\n");
    return 2;
  }
  std::string arg = argv[1];
  std::string host = arg;
  uint16_t port = kPort;
  if (auto c = arg.find(':'); c != std::string::npos) {
    host = arg.substr(0, c);
    port = static_cast<uint16_t>(std::stoi(arg.substr(c + 1)));
  }
  const double secs = argc > 2 ? std::stod(argv[2]) : 10.0;

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &a.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
    std::fprintf(stderr, "connect to %s:%u failed: %s\n", host.c_str(), port,
                 std::strerror(errno));
    return 1;
  }

  auto hello = build_client_hello(1920, 1080, "{droppix-0000-0000-0000-000000000001}",
                                  "droppix");
  if (::send(fd, hello.data(), hello.size(), 0) != static_cast<ssize_t>(hello.size())) {
    std::fprintf(stderr, "sending hello failed\n");
    return 1;
  }
  std::printf("sent droppix client hello (%zu bytes, declaring 1920x1080)\n", hello.size());

  ClientParser parser;
  std::map<uint32_t, int> counts;
  size_t jpeg_bytes = 0, frames = 0, valid_jpeg = 0;
  uint32_t dw = 0, dh = 0, covered = 0;
  bool tiles = true;

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(static_cast<int>(secs * 1000));
  unsigned char buf[65536];
  uint32_t seq = 3;
  auto last_ka = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd pfd{fd, POLLIN, 0};
    if (::poll(&pfd, 1, 300) <= 0) {
      // Keep the session alive the way a viewer does while it waits.
      if (std::chrono::steady_clock::now() - last_ka > std::chrono::seconds(1)) {
        auto k = build_client_keepalive(seq);
        seq += 2;
        last_ka = std::chrono::steady_clock::now();
        ::send(fd, k.data(), k.size(), MSG_NOSIGNAL);
      }
      continue;
    }
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    parser.feed(buf, static_cast<size_t>(n), [&](const ServerMessage& m) {
      counts[m.type]++;
      if (m.type != kMsgFrame) return;
      ++frames;
      jpeg_bytes += m.jpeg.size();
      dw = m.display_w;
      dh = m.display_h;
      const bool ok = m.jpeg.size() > 4 && m.jpeg[0] == 0xff && m.jpeg[1] == 0xd8 &&
                      m.jpeg[m.jpeg.size() - 2] == 0xff && m.jpeg[m.jpeg.size() - 1] == 0xd9;
      if (ok) ++valid_jpeg;
      if (m.y_start != covered) tiles = (m.y_start == 0);   // a new frame restarts at 0
      covered = m.y_end;
    });
  }
  ::close(fd);

  std::printf("\nmessages by type:\n");
  for (auto& [t, c] : counts) {
    const char* name = t == kMsgHeartbeat ? "heartbeat"
                     : t == kMsgFrame     ? "video (JPEG stripe)"
                     : t == kMsgDisplay   ? "display"
                     : t == kMsgAck       ? "ack" : "UNKNOWN";
    std::printf("  type %-3u %-22s x%d\n", t, name, c);
  }
  std::printf("\nremote display : %ux%u\n", dw, dh);
  std::printf("stripes        : %zu (%zu decoded as valid JFIF)\n", frames, valid_jpeg);
  std::printf("jpeg bytes     : %zu\n", jpeg_bytes);
  std::printf("stripes tile   : %s\n", tiles ? "yes" : "NO");
  std::printf("\n%s\n", (frames > 0 && valid_jpeg == frames)
                            ? "=> droppix's client consumed a real spacedesk server"
                            : "=> no usable video received");
  return (frames > 0 && valid_jpeg == frames) ? 0 : 1;
}
