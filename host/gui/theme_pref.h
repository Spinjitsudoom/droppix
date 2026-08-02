#pragma once
#include <string>
#include "theme.h"
namespace droppix {
Theme loadThemePref(const std::string& configDir);           // default Dark
void  saveThemePref(const std::string& configDir, Theme t);  // writes <dir>/theme
}  // namespace droppix
