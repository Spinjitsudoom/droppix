#pragma once
#include <vector>
#include "evdi_lib.h"

namespace droppix {

struct Frame {
  int width = 0, height = 0, stride = 0;
  std::vector<unsigned char> bgra;   // 32bpp, B,G,R,X byte order
  std::vector<evdi_rect> rects;      // changed regions
  bool valid = false;
};

class Capturer {
 public:
  explicit Capturer(evdi_handle h);
  ~Capturer();
  bool wait_for_mode(int timeout_ms);
  Frame grab(int timeout_ms);
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  bool wait_readable(int timeout_ms);
  void register_buffer();

  evdi_handle handle_;
  int width_ = 0, height_ = 0, stride_ = 0;
  int buffer_id_ = 1;
  std::vector<unsigned char> buffer_;
  bool buffer_registered_ = false;

  // event-loop scratch state, written by static handlers
  bool got_mode_ = false;
  bool update_ready_ = false;
  static void on_mode_changed(evdi_mode mode, void* user);
  static void on_update_ready(int buf, void* user);
};

}  // namespace droppix
