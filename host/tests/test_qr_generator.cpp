#include <gtest/gtest.h>
#include "qr_generator.h"

using namespace droppix;

TEST(QrGenerator, GenerateUriWithIp) {
  EXPECT_EQ(generate_qr_uri("192.168.1.100", 27000, "123456"),
            "droppix://192.168.1.100:27000?code=123456");
}

TEST(QrGenerator, GenerateUriWithHostname) {
  EXPECT_EQ(generate_qr_uri("my-pc.local", 27000, "654321"),
            "droppix://my-pc.local:27000?code=654321");
}

TEST(QrGenerator, GenerateUriWithNonStandardPort) {
  EXPECT_EQ(generate_qr_uri("localhost", 27001, "111111"),
            "droppix://localhost:27001?code=111111");
}

TEST(QrGenerator, ValidatePairingCode) {
  EXPECT_TRUE(is_valid_pairing_code("123456"));
  EXPECT_TRUE(is_valid_pairing_code("000000"));
  EXPECT_FALSE(is_valid_pairing_code("12345"));    // too short
  EXPECT_FALSE(is_valid_pairing_code("1234567"));  // too long
  EXPECT_FALSE(is_valid_pairing_code("12345a"));   // non-numeric
}
