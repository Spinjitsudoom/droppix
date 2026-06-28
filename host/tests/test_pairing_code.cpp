#include <gtest/gtest.h>
#include "pairing_code.h"

using namespace droppix;

TEST(PairingCode, DeterministicSixDigits) {
  std::vector<unsigned char> der = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04};
  std::string code = derive_pairing_code(der);
  EXPECT_EQ(code.size(), 6u);
  for (char c : code) EXPECT_TRUE(c >= '0' && c <= '9');
  EXPECT_EQ(code, derive_pairing_code(der));            // deterministic
  // Lock the exact value so the Kotlin side must match it byte-for-byte:
  EXPECT_EQ(code, "376946");
}
