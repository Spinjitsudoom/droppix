#pragma once
#include <memory>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // droppix::OutputInfo

namespace droppix {

// Session-command prefix so the root streamer can run user-session tools (kscreen,
// KWin DBus, pw-record) AS THE INVOKING USER via runuser + a reconstructed env.
// Returns "env " when already a user session. (Relocated from stream_daemon,
// unchanged: keeps WAYLAND_DISPLAY=wayland-0 — real-socket discovery is M2's job.)
std::string user_session_prefix();

// Validate a compositor output/connector name (connector-id chars only) before it is
//  interpolated into a shell command or trusted as identified. Shared by the KWin backend
//  and StreamDaemon's output-identification gate.
bool safe_output_name(const std::string& s);

enum class LayoutMode { Extend, Mirror };

// Per-desktop operations droppix needs beyond creating the evdi output. Compositing
// the virtual display, encode, and stream are compositor-agnostic and not here.
struct DesktopBackend {
  virtual ~DesktopBackend() = default;
  virtual const char* name() const = 0;                       // for logs: "kwin"/"generic"
  virtual std::vector<OutputInfo> outputs() = 0;              // enabled outputs w/ geometry
  virtual void map_touch(const std::string& output,
                         const std::string& touch_dev) = 0;   // best-effort; may no-op
  // Bind the uinput pen device to the droppix output, same idea as map_touch (else pen
  // strokes land on whatever output the device defaults to, not droppix's). Default
  // no-op (Generic); X11/KWin override. Returns true if a binding attempt was made
  // (fire-and-forget on X11) or actually succeeded (KWin confirms via DBus); best-effort,
  // failure just means the pen may land on the wrong monitor -- display/pen still work.
  virtual bool map_pen(const std::string& output, const std::string& pen_dev) {
    (void)output; (void)pen_dev; return false;
  }
  // Link any reverse-PRIME provider (evdi as a separate GPU) as a sink of provider 0 so
  // its output becomes visible/enable-able. Must run BEFORE the output can be identified
  // and moded. Default no-op (Wayland compositors handle this themselves); X11 overrides.
  // Returns true if a link command was issued.
  virtual bool link_providers() { return false; }
  // Called once the droppix output is identified. Wayland compositors adopt/place evdi
  // outputs themselves (default no-op); X11 must do it explicitly (reverse-PRIME provider
  // link + placement, or the desktop shows black). Returns true if the layout may have
  // changed so the caller re-queries geometry.
  virtual bool adopt_output(const std::string& output) { (void)output; return false; }
  // Apply mirror/extend layout for the droppix output relative to the primary display.
  // Default no-op (Generic); KWin/X11 override. Returns true if the command was issued.
  virtual bool apply_layout(const std::string& evdi_output, LayoutMode mode) {
    (void)evdi_output; (void)mode; return false;
  }
  // Place the droppix output at an absolute pixel position (client-declared monitor-grid
  // wall_col/wall_row -> pixel offset, computed by grid_position). Default no-op (Generic);
  // KWin/X11 override. Returns true if the command was issued.
  virtual bool place_output(const std::string& evdi_output, int x, int y) {
    (void)evdi_output; (void)x; (void)y; return false;
  }
};

// KDE Plasma: today's behavior, relocated (kscreen-doctor -o + KWin InputDevice DBus).
class KWinBackend : public DesktopBackend {
 public:
  const char* name() const override { return "kwin"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
  bool map_pen(const std::string& output, const std::string& pen_dev) override;
  bool apply_layout(const std::string& evdi_output, LayoutMode mode) override;
  bool place_output(const std::string& evdi_output, int x, int y) override;
};

// Non-KDE X11 desktops (Cinnamon, XFCE, MATE, GNOME-on-Xorg, i3, ...): xrandr for
// output geometry, `xinput map-to-output` to pin the touchscreen to the droppix output.
class X11Backend : public DesktopBackend {
 public:
  const char* name() const override { return "x11"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
  bool map_pen(const std::string& output, const std::string& pen_dev) override;
  bool link_providers() override;
  bool adopt_output(const std::string& output) override;
  bool apply_layout(const std::string& evdi_output, LayoutMode mode) override;
  bool place_output(const std::string& evdi_output, int x, int y) override;

 private:
  // Shared `xinput map-to-output` retry/settle script: touch and pen are both absolute
  // devices, so the binding logic is identical -- only the device name (and log tag)
  // differ. Returns true once the safety checks pass and the binding attempt was issued
  // (fire-and-forget completion, like adopt_output/apply_layout).
  bool map_device_x11(const std::string& output, const std::string& dev, const std::string& tag);
};

// Unknown/unsupported compositor: display still works (evdi is compositor-driven);
// outputs() returns {} and map_touch() logs a warning and no-ops.
class GenericBackend : public DesktopBackend {
 public:
  const char* name() const override { return "generic"; }
  std::vector<OutputInfo> outputs() override { return {}; }
  void map_touch(const std::string& output, const std::string& touch_dev) override;
};

enum class BackendKind { KWin, X11, Generic };

// PURE (unit-tested). "kde"/"plasma" in the desktop string (case-insensitive) OR
// (empty desktop AND kscreen-doctor present) -> KWin; else an X11 session with
// xrandr+xinput available -> X11; otherwise Generic.
BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen,
                                bool x11_session, bool has_x11_tools);

// PURE (unit-tested). Returns the compositor command (no user_session_prefix/timeout wrapper;
// the caller adds those). Empty string when unsupported (Generic) or when output names are
// unsafe.
std::string layout_command(BackendKind kind, const std::string& evdi_output,
                          const std::string& primary_output, int primary_id, LayoutMode mode);

// Absolute pixel position of a cell in the client-declared monitor grid (wall_col/wall_row),
// anchored at (anchor_x, anchor_y) -- typically the primary output's top-left corner.
struct GridPoint { int x, y; };

// PURE (unit-tested). cell -> pixel: anchor + col/row * own output size.
GridPoint grid_position(int col, int row, int own_w, int own_h, int anchor_x, int anchor_y);

// PURE (unit-tested). Returns the compositor command to place evdi_output at absolute pixel
// (x, y) (no user_session_prefix/timeout wrapper; the caller adds those). Empty string when
// unsupported (Generic) or when the output name is unsafe.
std::string place_command(BackendKind kind, const std::string& evdi_output, int x, int y);

// Detect the desktop (env + `command -v kscreen-doctor`), pick the backend, log it.
std::shared_ptr<DesktopBackend> make_desktop_backend();

}  // namespace droppix
