#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "png_writer.h"

namespace droppix {

std::vector<unsigned char> bgra_to_rgba(const std::vector<unsigned char>& bgra) {
  std::vector<unsigned char> rgba(bgra.size());
  for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
    rgba[i + 0] = bgra[i + 2];  // R <- B
    rgba[i + 1] = bgra[i + 1];  // G
    rgba[i + 2] = bgra[i + 0];  // B <- R
    rgba[i + 3] = 0xFF;         // opaque
  }
  return rgba;
}

bool save_png_from_bgra(const std::string& path, const Frame& f) {
  if (!f.valid || f.bgra.empty()) return false;
  std::vector<unsigned char> rgba = bgra_to_rgba(f.bgra);
  return stbi_write_png(path.c_str(), f.width, f.height, 4,
                        rgba.data(), f.stride) != 0;
}

}  // namespace droppix
