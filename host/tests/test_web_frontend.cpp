#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include "pairing_code.h"
#include "web_frontend.h"

using namespace droppix;

static std::string write_temp_cert() {
  char dir[] = "/tmp/droppix_web_XXXXXX";
  if (!mkdtemp(dir)) return "";
  std::string cert = std::string(dir) + "/cert.pem";
  std::string key = std::string(dir) + "/key.pem";
  std::string cmd = "openssl req -x509 -newkey rsa:2048 -nodes -keyout '" + key +
                    "' -out '" + cert + "' -days 1 -subj '/CN=droppix-test' 2>/dev/null";
  if (std::system(cmd.c_str()) != 0) return "";
  return cert;
}

TEST(WebFrontend, LoadCertDerAndPairingCode) {
  std::string cert = write_temp_cert();
  if (cert.empty()) GTEST_SKIP() << "openssl unavailable";
  auto der = load_cert_der_pem(cert);
  ASSERT_FALSE(der.empty());
  auto code = derive_pairing_code(der);
  EXPECT_EQ(code.size(), 6u);
  for (char c : code) EXPECT_TRUE(c >= '0' && c <= '9');
  EXPECT_EQ(code, derive_pairing_code(der));
}
