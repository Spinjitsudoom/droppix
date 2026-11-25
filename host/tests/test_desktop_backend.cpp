#include "desktop_backend.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(DesktopBackend, KdeDesktopSelectsKWin) {
  EXPECT_EQ(select_backend_kind("KDE", false, false, false), BackendKind::KWin);
}
TEST(DesktopBackend, PlasmaDesktopSelectsKWinCaseInsensitive) {
  EXPECT_EQ(select_backend_kind("plasma", false, false, false), BackendKind::KWin);
  EXPECT_EQ(select_backend_kind("KDE:plasmawayland", false, false, false), BackendKind::KWin);
}
TEST(DesktopBackend, UnknownDesktopWithKscreenSelectsKWin) {
  EXPECT_EQ(select_backend_kind("", true, false, false), BackendKind::KWin);
}
TEST(DesktopBackend, UnknownDesktopNoToolSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("", false, false, false), BackendKind::Generic);
}
TEST(DesktopBackend, GnomeWaylandSelectsGnome) {
  EXPECT_EQ(select_backend_kind("GNOME", false, false, false), BackendKind::Gnome);
  EXPECT_EQ(select_backend_kind("ubuntu:GNOME", false, false, false), BackendKind::Gnome);
}
TEST(DesktopBackend, MutterOnTheBusSelectsGnomeWithoutADesktopName) {
  // Under pkexec XDG_CURRENT_DESKTOP is gone; Mutter's D-Bus name is the reliable signal.
  EXPECT_EQ(select_backend_kind("", false, false, false, true), BackendKind::Gnome);
}
TEST(DesktopBackend, GnomeOnX11KeepsTheX11Backend) {
  EXPECT_EQ(select_backend_kind("GNOME", false, true, true, true), BackendKind::X11);
}
TEST(DesktopBackend, KdeBeatsMutter) {
  EXPECT_EQ(select_backend_kind("KDE", true, false, false, true), BackendKind::KWin);
}
TEST(DesktopBackend, NonKdeDesktopIgnoresKscreenPresence) {
  // A named non-KDE desktop never becomes KWin just because kscreen-doctor is installed;
  // the tool only promotes an UNKNOWN desktop.
  EXPECT_EQ(select_backend_kind("sway", true, false, false), BackendKind::Sway);
  EXPECT_EQ(select_backend_kind("Hyprland", true, false, false), BackendKind::Generic);
}

TEST(DesktopBackend, X11SessionWithToolsSelectsX11) {
  EXPECT_EQ(select_backend_kind("X-Cinnamon", false, true, true), BackendKind::X11);
  EXPECT_EQ(select_backend_kind("XFCE", false, true, true), BackendKind::X11);
}
TEST(DesktopBackend, X11SessionMissingToolsSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("X-Cinnamon", false, true, false), BackendKind::Generic);
}
TEST(DesktopBackend, KdeOnX11StillSelectsKWin) {
  // KDE keeps precedence over the generic X11 path (KWin's DBus binding is richer).
  EXPECT_EQ(select_backend_kind("KDE", true, true, true), BackendKind::KWin);
}
TEST(DesktopBackend, WaylandNonKdeWithToolsSelectsGeneric) {
  // xrandr/xinput being installed doesn't matter on a Wayland session (XWayland's view
  // of outputs/input isn't the compositor's).
  EXPECT_EQ(select_backend_kind("river", false, false, true), BackendKind::Generic);
}
TEST(DesktopBackend, UnknownDesktopOnX11WithToolsSelectsX11) {
  EXPECT_EQ(select_backend_kind("", false, true, true), BackendKind::X11);
}

using droppix::BackendKind; using droppix::LayoutMode; using droppix::layout_command;
TEST(LayoutCommand, KWinMirrorReplicates) {
  auto c = layout_command(BackendKind::KWin, "DVI-I-1", "DP-3", 1, LayoutMode::Mirror);
  EXPECT_NE(c.find("replicationSource.1"), std::string::npos);
  EXPECT_NE(c.find("DVI-I-1"), std::string::npos);
}
TEST(LayoutCommand, KWinExtendClears) {
  auto c = layout_command(BackendKind::KWin, "DVI-I-1", "DP-3", 1, LayoutMode::Extend);
  EXPECT_NE(c.find("replicationSource.0"), std::string::npos);
}
TEST(LayoutCommand, X11MirrorSameAs) {
  auto c = layout_command(BackendKind::X11, "DVI-I-1", "eDP-1", 0, LayoutMode::Mirror);
  EXPECT_NE(c.find("--same-as"), std::string::npos);
  EXPECT_NE(c.find("eDP-1"), std::string::npos);
}
TEST(LayoutCommand, X11ExtendRightOf) {
  auto c = layout_command(BackendKind::X11, "DVI-I-1", "eDP-1", 0, LayoutMode::Extend);
  EXPECT_NE(c.find("--right-of"), std::string::npos);
}
TEST(LayoutCommand, GenericEmpty) {
  EXPECT_TRUE(layout_command(BackendKind::Generic, "X", "Y", 0, LayoutMode::Mirror).empty());
}
TEST(LayoutCommand, UnsafeNameRejected) {
  EXPECT_TRUE(layout_command(BackendKind::X11, "X; rm -rf /", "Y", 0, LayoutMode::Mirror).empty());
}

using droppix::grid_position; using droppix::place_command; using droppix::BackendKind;
TEST(GridPosition, CellToPixel) {
  EXPECT_EQ(grid_position(0,0,1280,800, 1920,0).x, 1920);
  EXPECT_EQ(grid_position(0,0,1280,800, 1920,0).y, 0);
  auto p = grid_position(2,1,1280,800, 1920,0);
  EXPECT_EQ(p.x, 1920 + 2*1280); EXPECT_EQ(p.y, 800);
}
TEST(PlaceCommand, X11AndKwin) {
  EXPECT_NE(place_command(BackendKind::X11, "DVI-I-1", 3200, 0).find("--pos 3200x0"), std::string::npos);
  EXPECT_NE(place_command(BackendKind::KWin, "DVI-I-1", 3200, 800).find("position.3200,800"), std::string::npos);
  EXPECT_TRUE(place_command(BackendKind::Generic, "X", 0, 0).empty());
  EXPECT_TRUE(place_command(BackendKind::X11, "bad;name", 0, 0).empty());   // safe_output_name
}

// The root streamer reaches the user's compositor by socket name. It was hardcoded to
// wayland-0, but Plasma often runs on wayland-1, and then kscreen-doctor returned nothing,
// the droppix output was never identified, and touch was disabled.
TEST(DesktopBackend, PicksTheLiveWaylandSocket) {
  EXPECT_EQ(pick_wayland_display({"bus", "wayland-1", "wayland-1.lock", "pipewire-0"}), "wayland-1");
}
TEST(DesktopBackend, IgnoresAWaylandSocketWithoutItsLock) {
  // A socket left behind by a dead compositor has no lock file.
  EXPECT_EQ(pick_wayland_display({"wayland-0", "wayland-2", "wayland-2.lock"}), "wayland-2");
}
TEST(DesktopBackend, PrefersTheLowestLiveWaylandSocket) {
  EXPECT_EQ(pick_wayland_display({"wayland-3", "wayland-3.lock", "wayland-1", "wayland-1.lock"}),
            "wayland-1");
}
TEST(DesktopBackend, FallsBackToWayland0) {
  EXPECT_EQ(pick_wayland_display({"bus"}), "wayland-0");
  EXPECT_EQ(pick_wayland_display({"wayland-x", "wayland-x.lock"}), "wayland-0");
}

TEST(SessionDeviceId, DefaultNameKeepsTheBaseId) {
  EXPECT_EQ(session_device_id(0xd701, "droppix-touch"), 0xd701);
}
TEST(SessionDeviceId, EachSessionPortGetsItsOwnId) {
  // GNOME keys touch/tablet-to-monitor settings by vendor:product, so two tablets at once
  // need different ids or both map to one screen.
  const uint16_t a = session_device_id(0xd701, "droppix-touch-27000");
  const uint16_t b = session_device_id(0xd701, "droppix-touch-27001");
  EXPECT_NE(a, b);
  EXPECT_NE(a, 0xd701);
}
TEST(SessionDeviceId, TouchAndPenOfDifferentSessionsNeverCollide) {
  for (int p = 27000; p < 27064; ++p)
    for (int q = 27000; q < 27064; ++q)
      EXPECT_NE(session_device_id(0xd701, "droppix-touch-" + std::to_string(p)),
                session_device_id(0xd704, "droppix-touch-" + std::to_string(q)));
}
TEST(SessionDeviceId, SameSessionIsStable) {
  EXPECT_EQ(session_device_id(0xd704, "droppix-touch-27003"),
            session_device_id(0xd704, "droppix-touch-27003"));
}
