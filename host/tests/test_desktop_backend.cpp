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
