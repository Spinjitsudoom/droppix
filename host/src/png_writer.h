#pragma once
#include <string>
#include <vector>
#include "capturer.h"

namespace droppix {

// Pure helper: converts a BGRA byte buffer to RGBA, forcing alpha to opaque.
// Exposed separately so it can be unit-tested without touching disk/hardware.
std::vector<unsigned char> bgra_to_rgba(const std::vector<unsigned char>& bgra);

// Writes frame `f` (BGRA) to `path` as RGBA PNG. Returns success.
bool save_png_from_bgra(const std::string& path, const Frame& f);

}  // namespace droppix
