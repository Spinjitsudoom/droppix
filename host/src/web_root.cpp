#include "web_root.h"
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>

namespace droppix {
namespace {

bool has_index(const std::string& dir) {
  std::string p = dir;
  if (!p.empty() && p.back() != '/') p.push_back('/');
  p += "index.html";
  struct stat st {};
  return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

}  // namespace

std::string resolve_web_root() {
  if (const char* e = std::getenv("DROPPIX_WEB_ROOT"); e && *e) {
    if (has_index(e)) return e;
  }
  const char* candidates[] = {
      "web/dist",
      "../web/dist",
      "../../web/dist",
      "share/droppix/web",
      "../share/droppix/web",
  };
  char cwd[4096];
  if (::getcwd(cwd, sizeof(cwd))) {
    for (const char* rel : candidates) {
      std::string full = std::string(cwd) + "/" + rel;
      if (has_index(full)) return full;
    }
  }
  return {};
}

}  // namespace droppix
