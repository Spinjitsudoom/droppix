#include "theme_pref.h"
#include <fstream>
namespace droppix {
Theme loadThemePref(const std::string& configDir) {
  std::ifstream in(configDir + "/theme");
  std::string v; std::getline(in, v);
  return v == "light" ? Theme::Light : Theme::Dark;   // anything else -> Dark
}
void saveThemePref(const std::string& configDir, Theme t) {
  std::ofstream out(configDir + "/theme", std::ios::trunc);
  out << (t == Theme::Light ? "light" : "dark");
}
}  // namespace droppix
