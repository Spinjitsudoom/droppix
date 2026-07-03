#pragma once
#include <vector>
#include "evdi_lib.h"

namespace droppix {

class VirtualDisplay {
 public:
  ~VirtualDisplay();
  bool open();
  void connect(const std::vector<unsigned char>& edid);
  void disconnect();
  int node() const { return node_; }
  evdi_handle handle() const { return handle_; }

 private:
  evdi_handle handle_ = nullptr;
  int node_ = -1;
  bool connected_ = false;
  int lock_fd_ = -1;   // flock held for this session's life to own node_ across processes
};

}  // namespace droppix
