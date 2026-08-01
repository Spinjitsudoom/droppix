#include <gtest/gtest.h>
#include "autostart.h"
using namespace droppix;
TEST(Autostart, ExecPrefersAppImage) {
  EXPECT_EQ(autostart_exec_command({"/x/Droppix.AppImage", "org.droppix.Droppix", "/sandbox/bin"}),
            "/x/Droppix.AppImage");
}
TEST(Autostart, ExecFlatpakWhenNoAppImage) {
  EXPECT_EQ(autostart_exec_command({"", "org.droppix.Droppix", "/app/bin/droppix_gui"}),
            "flatpak run org.droppix.Droppix");
}
TEST(Autostart, ExecAppPathFallbackQuotesSpaces) {
  EXPECT_EQ(autostart_exec_command({"", "", "/usr/bin/droppix_gui"}), "/usr/bin/droppix_gui");
  EXPECT_EQ(autostart_exec_command({"", "", "/home/u/My Apps/droppix_gui"}),
            "\"/home/u/My Apps/droppix_gui\"");
}
TEST(Autostart, DesktopHasMinimizedAndAutostartFlag) {
  auto d = autostart_desktop("/usr/bin/droppix_gui");
  EXPECT_NE(d.find("Exec=/usr/bin/droppix_gui --minimized\n"), std::string::npos);
  EXPECT_NE(d.find("X-GNOME-Autostart-enabled=true"), std::string::npos);
  EXPECT_NE(d.find("Type=Application"), std::string::npos);
}
