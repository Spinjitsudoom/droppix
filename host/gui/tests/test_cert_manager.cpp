#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include "cert_manager.h"

using namespace droppix;

namespace {
// Runs an openssl command and returns stdout; empty string if openssl is unavailable
// or the command failed (tests using this SKIP rather than fail in that case, matching
// test_web_frontend.cpp's existing "openssl unavailable" convention).
std::string run(const std::string& cmd) {
  FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
  if (!p) return "";
  std::string out;
  char buf[256];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);
  return out;
}

bool has_openssl() { return std::system("command -v openssl >/dev/null 2>&1") == 0; }
}  // namespace

TEST(CertManager, EnsureGeneratesCaAndLeafSignedByIt) {
  if (!has_openssl()) GTEST_SKIP() << "openssl unavailable";
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  CertManager cm(dir.path());
  ASSERT_TRUE(cm.ensure());
  EXPECT_TRUE(QFile::exists(cm.caPath()));
  EXPECT_TRUE(QFile::exists(cm.caKeyPath()));
  EXPECT_TRUE(QFile::exists(cm.certPath()));
  EXPECT_TRUE(QFile::exists(cm.keyPath()));

  // The CA verifies the leaf: this is the actual property a browser checks.
  const std::string verify = run("openssl verify -CAfile '" + cm.caPath().toStdString() +
                                 "' '" + cm.certPath().toStdString() + "'");
  EXPECT_NE(verify.find("OK"), std::string::npos) << verify;

  // Leaf must carry at least one SAN entry (bare CN is not enough for modern browsers).
  const std::string text = run("openssl x509 -in '" + cm.certPath().toStdString() +
                               "' -noout -text");
  EXPECT_NE(text.find("Subject Alternative Name"), std::string::npos);
  EXPECT_NE(text.find("127.0.0.1"), std::string::npos);
}

TEST(CertManager, RegenerateKeepsTheSameCa) {
  if (!has_openssl()) GTEST_SKIP() << "openssl unavailable";
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  CertManager cm(dir.path());
  ASSERT_TRUE(cm.ensure());
  const std::string ca_before = run("openssl x509 -in '" + cm.caPath().toStdString() +
                                    "' -noout -fingerprint -sha256");
  const std::string code_before = cm.pairingCode().toStdString();

  ASSERT_TRUE(cm.regenerate());
  const std::string ca_after = run("openssl x509 -in '" + cm.caPath().toStdString() +
                                   "' -noout -fingerprint -sha256");
  EXPECT_EQ(ca_before, ca_after) << "regenerate() must never rotate the CA — that would "
                                    "untrust every device that already installed it";
  EXPECT_NE(cm.pairingCode().toStdString(), code_before)
      << "the leaf (and its derived pairing code) must still rotate per launch";

  // The freshly-regenerated leaf is still signed by the (unchanged) CA.
  const std::string verify = run("openssl verify -CAfile '" + cm.caPath().toStdString() +
                                 "' '" + cm.certPath().toStdString() + "'");
  EXPECT_NE(verify.find("OK"), std::string::npos) << verify;
}
