#include "virtual_display.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

namespace droppix {

namespace {
constexpr char kLockDir[] = "/run/droppix";

// evdi_check_device() reports AVAILABLE for a node another PROCESS already has open
// and connected — libevdi can't see across processes. So find_available_node() alone
// hands every concurrent droppix session node 0, and all but the first collide (they
// open the node as a DRM slave and start() fails). We additionally claim a node with a
// process-lifetime flock: a node is only ours if evdi says AVAILABLE AND no other
// droppix session holds its lock. The lock releases automatically when the process
// exits, so a freed node is reclaimable by the next session (no unbounded growth).
//
// Returns a held fd (>=0) on success — keep it open for the session's life — or -1 if
// another session owns the node.
int try_claim_node(int node) {
  ::mkdir(kLockDir, 0755);   // idempotent; streamer runs as root
  char path[64];
  std::snprintf(path, sizeof(path), "%s/evdi-node-%d.lock", kLockDir, node);
  int fd = ::open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0) return -1;
  if (::flock(fd, LOCK_EX | LOCK_NB) < 0) { ::close(fd); return -1; }  // owned elsewhere
  return fd;
}

// Scan for the first evdi node that is both AVAILABLE (per libevdi) and claimable
// (not flock-held by another session). On success sets *lock_fd and returns the node.
int claim_available_node(int* lock_fd) {
  for (int i = 0; i < 16; ++i) {
    if (evdi_check_device(i) != AVAILABLE) continue;
    int fd = try_claim_node(i);
    if (fd >= 0) { *lock_fd = fd; return i; }
  }
  return -1;
}
}  // namespace

bool VirtualDisplay::open() {
  // Serialize node selection across concurrent streamer processes so two sessions
  // starting at once don't both add a device or race for the same node.
  ::mkdir(kLockDir, 0755);
  int glock = ::open("/run/droppix/select.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (glock >= 0) ::flock(glock, LOCK_EX);

  node_ = claim_available_node(&lock_fd_);
  if (node_ < 0) {
    // Nothing claimable: create a fresh evdi device for this session (needs root).
    if (evdi_add_device() < 0) {
      std::fprintf(stderr, "evdi_add_device failed (try running with sudo)\n");
      if (glock >= 0) ::close(glock);
      return false;
    }
    // evdi_add_device blocks until the new card exists, but give udev a moment for
    // the /dev/dri node to settle before we probe/claim it.
    for (int tries = 0; tries < 20 && node_ < 0; ++tries) {
      node_ = claim_available_node(&lock_fd_);
      if (node_ < 0) ::usleep(100000);   // 100ms
    }
  }

  if (glock >= 0) ::close(glock);   // releases the selection lock; node lock stays held

  if (node_ < 0) {
    std::fprintf(stderr, "no claimable evdi node found\n");
    return false;
  }
  handle_ = evdi_open(node_);
  if (handle_ == EVDI_INVALID_HANDLE) {
    std::fprintf(stderr, "evdi_open(%d) failed\n", node_);
    handle_ = nullptr;
    if (lock_fd_ >= 0) { ::close(lock_fd_); lock_fd_ = -1; }
    return false;
  }
  std::fprintf(stderr, "opened evdi node %d\n", node_);
  return true;
}

void VirtualDisplay::connect(const std::vector<unsigned char>& edid) {
  if (!handle_) {
    std::fprintf(stderr, "connect() called without a successful open()\n");
    return;
  }
  if (connected_) return;  // already connected; ignore duplicate connect
  // pixel_area_limit / pixel_per_second_limit = 0 means "no limit".
  evdi_connect2(handle_, edid.data(),
                static_cast<unsigned>(edid.size()), 0, 0);
  connected_ = true;
}

void VirtualDisplay::disconnect() {
  if (connected_ && handle_) {
    evdi_disconnect(handle_);
    connected_ = false;
  }
}

VirtualDisplay::~VirtualDisplay() {
  disconnect();
  if (handle_) evdi_close(handle_);
  if (lock_fd_ >= 0) ::close(lock_fd_);   // frees the node for the next session
}

}  // namespace droppix
