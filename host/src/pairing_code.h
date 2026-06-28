#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <openssl/sha.h>
namespace droppix {
inline std::string derive_pairing_code(const std::vector<unsigned char>& der) {
  unsigned char h[SHA256_DIGEST_LENGTH];
  SHA256(der.data(), der.size(), h);
  uint32_t u = (uint32_t(h[0])<<24)|(uint32_t(h[1])<<16)|(uint32_t(h[2])<<8)|uint32_t(h[3]);
  char buf[8]; std::snprintf(buf, sizeof buf, "%06u", u % 1000000u);
  return std::string(buf);
}
}  // namespace droppix
