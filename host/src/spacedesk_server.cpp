#include "spacedesk_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>

#include <chrono>
#include <vector>

#include "jpeg_stripe_encoder.h"
#include "spacedesk_protocol.h"

namespace droppix {
namespace {

// Write everything or fail; MSG_NOSIGNAL so a viewer vanishing mid-frame cannot kill the
// process (the same SIGPIPE trap that once took down the whole streamer).
bool send_all(int fd, const unsigned char* p, size_t n) {
  while (n) {
    ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
    if (w <= 0) {
      if (w < 0 && (errno == EINTR)) continue;
      return false;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool recv_exact(int fd, unsigned char* p, size_t n, int timeout_ms) {
  size_t got = 0;
  while (got < n) {
    pollfd pfd{fd, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
    ssize_t r = ::recv(fd, p + got, n - got, 0);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

// Subnet broadcast address of every up, non-loopback IPv4 interface.
//
// Sending to 255.255.255.255 picks a single interface by route, which on a host with
// docker/virtual bridges is frequently the wrong one — the viewer on the real LAN then
// never sees the announcement. Per-interface subnet broadcasts route correctly on each.
std::vector<in_addr_t> broadcast_targets() {
  std::vector<in_addr_t> out;
  ifaddrs* ifa = nullptr;
  if (getifaddrs(&ifa) != 0) return out;
  for (ifaddrs* p = ifa; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
    if (!(p->ifa_flags & IFF_BROADCAST) || !p->ifa_netmask) continue;
    const auto addr = reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr;
    const auto mask = reinterpret_cast<sockaddr_in*>(p->ifa_netmask)->sin_addr.s_addr;
    out.push_back((addr & mask) | ~mask);
  }
  freeifaddrs(ifa);
  return out;
}

}  // namespace

SpacedeskServer::SpacedeskServer(SourceFactory make_source, std::string machine_name,
                                 int jpeg_quality)
    : make_source_(std::move(make_source)),
      machine_name_(std::move(machine_name)),
      jpeg_quality_(jpeg_quality),
      port_(spacedesk::kPort) {}

SpacedeskServer::~SpacedeskServer() { stop(); }

bool SpacedeskServer::start() {
  if (running_.load()) return true;
  stopping_.store(false);

  listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);  // never inherited by popen() children
  if (listen_fd_ < 0) return false;
  int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons(port_);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 ||
      ::listen(listen_fd_, 4) != 0) {
    // Most likely a real spacedesk server (or another droppix) already owns the port.
    std::fprintf(stderr, "spacedesk: port %u unavailable (%s); compatibility server off\n",
                 port_, std::strerror(errno));
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  udp_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (udp_fd_ >= 0) {
    setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(udp_fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    if (::bind(udp_fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
      ::close(udp_fd_);
      udp_fd_ = -1;   // discovery off; a viewer can still connect by IP
    }
  }

  running_.store(true);
  accept_thread_ = std::thread([this] { run(); });
  if (udp_fd_ >= 0) discovery_thread_ = std::thread([this] { discovery_loop(); });
  std::fprintf(stderr, "spacedesk: compatibility server on :%u (name=%s)\n",
               port_, machine_name_.c_str());
  return true;
}

void SpacedeskServer::stop() {
  if (!running_.exchange(false)) return;
  stopping_.store(true);
  // Closing the fds unblocks accept()/recvfrom() so the threads can observe stopping_.
  if (listen_fd_ >= 0) { ::shutdown(listen_fd_, SHUT_RDWR); ::close(listen_fd_); listen_fd_ = -1; }
  if (udp_fd_ >= 0) { ::shutdown(udp_fd_, SHUT_RDWR); ::close(udp_fd_); udp_fd_ = -1; }
  if (accept_thread_.joinable()) accept_thread_.join();
  if (discovery_thread_.joinable()) discovery_thread_.join();
}

void SpacedeskServer::announce(const std::vector<unsigned char>& reply) {
  for (in_addr_t b : broadcast_targets()) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port_);
    to.sin_addr.s_addr = b;
    ::sendto(udp_fd_, reply.data(), reply.size(), 0,
             reinterpret_cast<sockaddr*>(&to), sizeof(to));
  }
}

void SpacedeskServer::discovery_loop() {
  const auto reply = spacedesk::build_discovery_response(machine_name_, port_);
  auto last_announce = std::chrono::steady_clock::now();
  while (!stopping_.load()) {
    // Announce unsolicited every 2s: a viewer whose probe or our reply was lost (or that
    // listens passively) still finds us, instead of the user seeing "no primary machine"
    // with no way to tell why.
    const auto now_a = std::chrono::steady_clock::now();
    if (now_a - last_announce > std::chrono::seconds(2)) {
      last_announce = now_a;
      announce(reply);
    }
    pollfd pfd{udp_fd_, POLLIN, 0};
    if (::poll(&pfd, 1, 500) <= 0) continue;
    unsigned char buf[512];
    sockaddr_in from{};
    socklen_t flen = sizeof(from);
    ssize_t n = ::recvfrom(udp_fd_, buf, sizeof(buf), 0,
                           reinterpret_cast<sockaddr*>(&from), &flen);
    if (n <= 0) continue;
    if (!spacedesk::is_discovery_request(buf, static_cast<size_t>(n))) continue;
    // Log that a viewer is probing us, throttled: the app broadcasts ~33x/second, so an
    // unthrottled line would drown the log. Without this the responder is invisible and
    // "the client can't see me" is undiagnosable — you cannot tell a broken reply from a
    // phone that never reached the network.
    ++discovery_seen_;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_discovery_log_ > std::chrono::seconds(5)) {
      last_discovery_log_ = now;
      char ip[INET_ADDRSTRLEN] = {0};
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
      std::fprintf(stderr, "spacedesk: answering discovery from %s (%llu probes so far)\n",
                   ip, static_cast<unsigned long long>(discovery_seen_));
    }
    // Answer the asking client directly — both its ephemeral source port and the
    // well-known one it listens on — then broadcast, which is what demonstrably makes a
    // real viewer list the server.
    ::sendto(udp_fd_, reply.data(), reply.size(), 0,
             reinterpret_cast<sockaddr*>(&from), flen);
    sockaddr_in wk = from;
    wk.sin_port = htons(port_);
    ::sendto(udp_fd_, reply.data(), reply.size(), 0,
             reinterpret_cast<sockaddr*>(&wk), sizeof(wk));
    announce(reply);
  }
}

void SpacedeskServer::run() {
  while (!stopping_.load()) {
    pollfd pfd{listen_fd_, POLLIN, 0};
    if (::poll(&pfd, 1, 500) <= 0) continue;
    sockaddr_in cli{};
    socklen_t clen = sizeof(cli);
    int fd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&cli), &clen, SOCK_CLOEXEC);
    if (fd < 0) continue;
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
    std::fprintf(stderr, "spacedesk: viewer connected from %s\n", ip);
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    client_connected_.store(true);
    serve_client(fd);
    client_connected_.store(false);
    ::close(fd);
    std::fprintf(stderr, "spacedesk: viewer disconnected (%s)\n", ip);
  }
}

void SpacedeskServer::serve_client(int fd) {
  using namespace spacedesk;

  // 1. The viewer speaks first: a hello carrying the size of ITS screen.
  unsigned char hdr[kHeaderSize];
  if (!recv_exact(fd, hdr, kHeaderSize, 10000)) return;
  Header h;
  if (!parse_header(hdr, kHeaderSize, h)) return;
  std::vector<unsigned char> body(h.payload_len);
  if (h.payload_len && !recv_exact(fd, body.data(), body.size(), 10000)) return;

  ClientHello hello;
  if (!parse_client_hello(hdr, kHeaderSize, hello)) {
    std::fprintf(stderr, "spacedesk: first message was type %u, not a hello\n", h.type);
    return;
  }
  const int w = static_cast<int>(hello.width);
  const int disp_h = static_cast<int>(hello.height);
  std::fprintf(stderr, "spacedesk: viewer screen %dx%d\n", w, disp_h);

  // 2. Replay the real server's opening sequence: ack, two display messages, two
  // heartbeats. The viewer stays connected with its display OFF until it gets these —
  // sending the ack alone produces exactly that symptom.
  for (const auto& m : build_session_open()) {
    if (!send_all(fd, m.data(), m.size())) return;
  }

  // 3. Build the display at the viewer's resolution, exactly as the native path does.
  auto src = make_source_ ? make_source_(w, disp_h) : nullptr;
  int aw = w, ah = disp_h;
  if (!src || !src->start(aw, ah)) {
    std::fprintf(stderr, "spacedesk: frame source failed to start\n");
    return;
  }

  uint32_t y0 = 0, y1 = 0;
  stripe_bounds(static_cast<uint32_t>(ah), 0, y0, y1);
  JpegStripeEncoder enc;
  if (!enc.open(aw, static_cast<int>(y1 - y0), jpeg_quality_)) {
    std::fprintf(stderr, "spacedesk: jpeg encoder failed to open\n");
    return;
  }

  // 4. Stream stripes until the viewer goes away.
  auto last_beat = std::chrono::steady_clock::now();
  while (!stopping_.load()) {
    // The real server keeps the session alive with a heartbeat every ~10s.
    const auto now_hb = std::chrono::steady_clock::now();
    if (now_hb - last_beat > std::chrono::seconds(10)) {
      last_beat = now_hb;
      const auto hb = build_heartbeat();
      if (!send_all(fd, hb.data(), hb.size())) return;
    }
    Frame f = src->next(16);
    if (!f.valid) {
      // Nothing changed. Drain any viewer input so a closed socket is noticed promptly.
      pollfd pfd{fd, POLLIN, 0};
      if (::poll(&pfd, 1, 0) > 0) {
        unsigned char scratch[256];
        if (::recv(fd, scratch, sizeof(scratch), MSG_DONTWAIT) == 0) return;
      }
      continue;
    }
    for (int i = 0; i < kStripes; ++i) {
      uint32_t s = 0, e = 0;
      stripe_bounds(static_cast<uint32_t>(f.height), i, s, e);
      const int rows = static_cast<int>(e - s);
      if (rows != enc.height() && !enc.open(f.width, rows, jpeg_quality_)) return;
      auto jpg = enc.encode(f.bgra.data(), f.stride, static_cast<int>(s), rows);
      if (jpg.empty()) continue;
      auto head = build_frame_header(static_cast<uint32_t>(f.width),
                                     static_cast<uint32_t>(f.height), s, e,
                                     static_cast<uint32_t>(jpg.size()));
      if (!send_all(fd, head.data(), head.size())) return;
      if (!send_all(fd, jpg.data(), jpg.size())) return;
    }
    frames_sent_.fetch_add(1);
  }
}

}  // namespace droppix
