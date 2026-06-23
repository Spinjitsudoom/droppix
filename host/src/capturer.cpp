#include "capturer.h"
#include <poll.h>
#include <cstdio>
#include <cstring>

namespace droppix {

// evdi caps accumulated dirty rectangles at MAX_DIRTS=16: the kernel painter
// merges rects once it reaches that count (module/evdi_painter.c) and libevdi
// copies at most num_rects (<=16) into the caller's buffer (library/evdi_lib.c).
// The grab buffer must therefore hold at least this many entries.
static constexpr int kEvdiMaxDirtyRects = 16;

Capturer::Capturer(evdi_handle h) : handle_(h) {}

Capturer::~Capturer() {
  if (buffer_registered_) evdi_unregister_buffer(handle_, buffer_id_);
}

void Capturer::on_mode_changed(evdi_mode mode, void* user) {
  auto* self = static_cast<Capturer*>(user);
  self->width_ = mode.width;
  self->height_ = mode.height;
  self->stride_ = mode.width * 4;  // 32bpp
  if (mode.bits_per_pixel != 32) {
    std::fprintf(stderr,
        "warning: evdi mode reports %d bpp; capturer assumes 32bpp\n",
        mode.bits_per_pixel);
  }
  self->got_mode_ = true;
  std::fprintf(stderr, "mode changed: %dx%d @ %d bpp\n",
               mode.width, mode.height, mode.bits_per_pixel);
}

void Capturer::on_update_ready(int /*buf*/, void* user) {
  static_cast<Capturer*>(user)->update_ready_ = true;
}

bool Capturer::wait_readable(int timeout_ms) {
  struct pollfd pfd{evdi_get_event_ready(handle_), POLLIN, 0};
  return poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

void Capturer::register_buffer() {
  buffer_.assign(static_cast<size_t>(stride_) * height_, 0);
  evdi_buffer b{};
  b.id = buffer_id_;
  b.buffer = buffer_.data();
  b.width = width_;
  b.height = height_;
  b.stride = stride_;
  b.rects = nullptr;     // filled by evdi_grab_pixels
  b.rect_count = 0;
  evdi_register_buffer(handle_, b);
  buffer_registered_ = true;
}

bool Capturer::wait_for_mode(int timeout_ms) {
  evdi_event_context ctx{};
  ctx.mode_changed_handler = &Capturer::on_mode_changed;
  ctx.user_data = this;
  got_mode_ = false;
  while (!got_mode_) {
    if (!wait_readable(timeout_ms)) return false;
    evdi_handle_events(handle_, &ctx);
  }
  register_buffer();
  return true;
}

Frame Capturer::grab(int timeout_ms) {
  Frame f;
  if (!buffer_registered_) return f;

  evdi_event_context ctx{};
  ctx.update_ready_handler = &Capturer::on_update_ready;
  // NOTE (Phase 0 limitation): if a mode change arrives mid-session, width_/
  // stride_ update but the framebuffer is not re-registered, so Frame metadata
  // could mismatch buffer_ contents. Re-registration is handled in a later phase.
  ctx.mode_changed_handler = &Capturer::on_mode_changed;
  ctx.user_data = this;

  update_ready_ = false;
  // If the update is immediately ready, evdi_request_update returns true.
  bool ready = evdi_request_update(handle_, buffer_id_);
  if (!ready) {
    if (!wait_readable(timeout_ms)) return f;
    evdi_handle_events(handle_, &ctx);
    if (!update_ready_) return f;
  }

  evdi_rect rects[kEvdiMaxDirtyRects];
  int num = 0;
  evdi_grab_pixels(handle_, rects, &num);
  // Defensive: never trust the out-param to exceed the documented cap.
  if (num < 0) num = 0;
  if (num > kEvdiMaxDirtyRects) num = kEvdiMaxDirtyRects;

  f.width = width_;
  f.height = height_;
  f.stride = stride_;
  f.bgra = buffer_;  // copy current contents
  f.rects.assign(rects, rects + num);
  f.valid = true;
  return f;
}

}  // namespace droppix
