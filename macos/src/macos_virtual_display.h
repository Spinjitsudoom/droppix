#pragma once
#include <CoreGraphics/CoreGraphics.h>

namespace droppix {

// Wraps the private CGVirtualDisplay API (no public headers; declarations are
// vendored in macos_virtual_display.mm) to create a real second display that
// macOS treats like any other monitor — System Settings > Displays, window
// placement, Mission Control all see it. Refresh is capped at 60 Hz (an API
// limit, not a choice). Closing drops the strong ref, which tears the display
// down; there's no separate "disconnect" step like evdi's EDID handshake.
class MacVirtualDisplay {
 public:
  ~MacVirtualDisplay();
  bool open(int width, int height, int refresh_hz);
  void close();
  CGDirectDisplayID display_id() const { return display_id_; }

 private:
  void* display_ = nullptr;       // CGVirtualDisplay*, retained
  void* descriptor_ = nullptr;    // CGVirtualDisplayDescriptor*, retained
  CGDirectDisplayID display_id_ = kCGNullDirectDisplay;
};

}  // namespace droppix
