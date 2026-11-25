#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // droppix::OutputInfo

namespace droppix {

// Session-command prefix so the root streamer can run user-session tools (kscreen,
// KWin DBus, pw-record) AS THE INVOKING USER via runuser + a reconstructed env.
// Returns "env " when already a user session. WAYLAND_DISPLAY is discovered from the
// user's runtime dir (pick_wayland_display), not assumed.
std::string user_session_prefix();

// PURE (unit-tested). Given the entries of $XDG_RUNTIME_DIR, the live compositor socket:
// the lowest-numbered "wayland-N" that also has its "wayland-N.lock" (a dead compositor
// leaves a socket but no lock). "wayland-0" if none qualifies.
std::string pick_wayland_display(const std::vector<std::string>& runtime_entries);

// PURE (unit-tested). Is any wayland-N socket in $XDG_RUNTIME_DIR live (has its .lock)? Then the
// session is Wayland even if XWayland's /tmp/.X11-unix/X0 exists. Same rule as
// pick_wayland_display, without its wayland-0 fallback.
bool has_live_wayland_socket(const std::vector<std::string>& runtime_entries);

// PURE (unit-tested). Extra environment for user-session tools when the session is Wayland:
// without it Qt 5 tools (Plasma 5's kscreen-doctor) default to xcb, fail to open XWayland
// under the root streamer, and print nothing. "" for an X11 session. Shell-quoted for a
// command line (never inside another single-quoted string).
std::string wayland_session_env(const std::vector<std::string>& runtime_entries);

// PURE (unit-tested). Does EDID serial text from a compositor ("27000", "0x00006978")
// equal the session's numeric serial?
bool edid_serial_matches(const std::string& text, uint32_t serial);

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
  // The output whose EDID serial is `edid_serial` (this session's), or "" if the backend
  // can't tell. Preferred over "what appeared since the session started", which fails when
  // the previous session's output hasn't been removed yet. Default: can't tell.
  virtual std::string find_output(uint32_t edid_serial) { (void)edid_serial; return {}; }
  // The source has had no mode for a few seconds: make the compositor mode the output
  // again (see FrameSource::start on_stalled). Default no-op. Returns true if it acted.
  virtual bool revive_output(uint32_t edid_serial) { (void)edid_serial; return false; }
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

// GNOME on Wayland (Mutter): org.gnome.Mutter.DisplayConfig over gdbus for outputs,
// placement and mirror; GNOME's per-device gsettings to pin touch/pen to the droppix
// monitor. Layout changes are applied as temporary, so the user's saved monitors.xml
// is never rewritten.
class GnomeBackend : public DesktopBackend {
 public:
  const char* name() const override { return "gnome"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
  bool map_pen(const std::string& output, const std::string& pen_dev) override;
  bool apply_layout(const std::string& evdi_output, LayoutMode mode) override;
  bool place_output(const std::string& evdi_output, int x, int y) override;
  std::string find_output(uint32_t edid_serial) override;
  bool revive_output(uint32_t edid_serial) override;
};

// Sway (wlroots): swaymsg JSON for outputs/placement, `input <id> map_to_output` for touch
// and pen. Sway has no output mirroring, so mirror stays extended. Sway only adopts an evdi
// output when its environment has WLR_EVDI_RENDER_DEVICE (a user setup step).
class SwayBackend : public DesktopBackend {
 public:
  const char* name() const override { return "sway"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
  bool map_pen(const std::string& output, const std::string& pen_dev) override;
  bool apply_layout(const std::string& evdi_output, LayoutMode mode) override;
  bool place_output(const std::string& evdi_output, int x, int y) override;
  std::string find_output(uint32_t edid_serial) override;
  bool revive_output(uint32_t edid_serial) override;
};

// Hyprland: `hyprctl -j` for outputs, `keyword monitor` rules for placement and mirror,
// per-device `device[<name>]:output` for touch and pen.
class HyprlandBackend : public DesktopBackend {
 public:
  const char* name() const override { return "hyprland"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
  bool map_pen(const std::string& output, const std::string& pen_dev) override;
  bool apply_layout(const std::string& evdi_output, LayoutMode mode) override;
  bool place_output(const std::string& evdi_output, int x, int y) override;
  std::string find_output(uint32_t edid_serial) override;
};

// Unknown/unsupported compositor: display still works (evdi is compositor-driven);
// outputs() returns {} and map_touch() logs a warning and no-ops.
class GenericBackend : public DesktopBackend {
 public:
  const char* name() const override { return "generic"; }
  std::vector<OutputInfo> outputs() override { return {}; }
  void map_touch(const std::string& output, const std::string& touch_dev) override;
};

enum class BackendKind { KWin, X11, Gnome, Sway, Hyprland, Generic };

// PURE (unit-tested). In order: "kde"/"plasma" in the desktop string -> KWin. On a Wayland
// session, GNOME ("gnome" or Mutter's DisplayConfig on the bus), Sway ("sway" or a live IPC
// socket), Hyprland ("hyprland" or a live instance) -> that backend; these live signals beat
// the next rule. Empty desktop AND kscreen-doctor present -> KWin. X11 session with
// xrandr+xinput -> X11. Then GNOME/Sway/Hyprland by name, else Generic.
BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen,
                                bool x11_session, bool has_x11_tools, bool has_mutter = false,
                                bool has_sway = false, bool has_hypr = false);

// PURE (unit-tested). USB product id for one session's uinput device. GNOME binds a
// touchscreen/tablet to a monitor by vendor:product, so concurrent sessions need distinct
// ids: a touch name ending in a number (the GUI uses "droppix-touch-<port>") shifts `base`
// by a per-session multiple of 0x10, keeping the low nibble (which tells the device kinds
// apart). A name without a number keeps `base`.
uint16_t session_device_id(uint16_t base, const std::string& touch_name);

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
