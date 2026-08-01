#include "autostart.h"

namespace droppix {

std::string autostart_exec_command(const AutostartEnv& e) {
  auto quote = [](const std::string& p) {
    return p.find(' ') != std::string::npos ? "\"" + p + "\"" : p;
  };
  if (!e.appimage.empty())   return quote(e.appimage);
  if (!e.flatpak_id.empty()) return "flatpak run " + e.flatpak_id;
  return quote(e.app_path);
}

std::string autostart_desktop(const std::string& exec_cmd) {
  return "[Desktop Entry]\n"
         "Type=Application\n"
         "Name=Droppix\n"
         "Comment=Use a tablet as a second monitor\n"
         "Exec=" + exec_cmd + " --minimized\n"
         "Icon=droppix\n"
         "Terminal=false\n"
         "X-GNOME-Autostart-enabled=true\n";
}

}  // namespace droppix
