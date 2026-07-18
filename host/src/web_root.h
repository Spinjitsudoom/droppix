#pragma once
#include <string>

namespace droppix {

// Resolve directory containing index.html for --web-root.
// Order: DROPPIX_WEB_ROOT, then common relative paths from CWD / exe-adjacent guesses.
std::string resolve_web_root();

}  // namespace droppix
