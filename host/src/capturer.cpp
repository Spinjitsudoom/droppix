#include "capturer.h"
#include <poll.h>
#include <cstdio>
#include <cstring>

namespace droppix {

Capturer::Capturer(evdi_handle h) : handle_(h) {}

Capturer::~Capturer() {
  if (buffer_registered_) evdi_unregister_buffer(handle_, buffer_id_);
}

void Capturer::on_mode_changed(evdi_mode mode, void* user) {
  auto* self = static_cast<Capturer*>(user);
  self->width_ = mode.width;
  self->height_ = mode.height;
  self->stride_ = mode.width * 4;  // 32bpp
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

  evdi_rect rects[16];
  int num = 0;
  evdi_grab_pixels(handle_, rects, &num);

  f.width = width_;
  f.height = height_;
  f.stride = stride_;
  f.bgra = buffer_;  // copy current contents
  f.rects.assign(rects, rects + num);
  f.valid = true;
  return f;
}

}  // namespace droppix
