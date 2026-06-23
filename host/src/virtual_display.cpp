#include "virtual_display.h"
#include <cstdio>

namespace droppix {

static int find_available_node() {
  for (int i = 0; i < 16; ++i) {
    if (evdi_check_device(i) == AVAILABLE) return i;
  }
  return -1;
}

bool VirtualDisplay::open() {
  node_ = find_available_node();
  if (node_ < 0) {
    // No free node: ask the kernel to add one (may require root).
    if (evdi_add_device() < 0) {
      std::fprintf(stderr, "evdi_add_device failed (try running with sudo)\n");
      return false;
    }
    node_ = find_available_node();
  }
  if (node_ < 0) {
    std::fprintf(stderr, "no AVAILABLE evdi node found\n");
    return false;
  }
  handle_ = evdi_open(node_);
  if (handle_ == EVDI_INVALID_HANDLE) {
    std::fprintf(stderr, "evdi_open(%d) failed\n", node_);
    handle_ = nullptr;
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
}

}  // namespace droppix
