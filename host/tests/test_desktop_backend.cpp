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
TEST(DesktopBackend, GnomeSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("GNOME", false, false, false), BackendKind::Generic);
}
TEST(DesktopBackend, NonKdeDesktopIgnoresKscreenPresence) {
  // A named non-KDE desktop is Generic even if kscreen-doctor happens to be installed;
  // the tool only promotes an UNKNOWN desktop.
  EXPECT_EQ(select_backend_kind("sway", true, false, false), BackendKind::Generic);
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
  EXPECT_EQ(select_backend_kind("GNOME", false, false, true), BackendKind::Generic);
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
