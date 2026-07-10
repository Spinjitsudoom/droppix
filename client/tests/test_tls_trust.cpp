#include "tls_trust.h"
#include <gtest/gtest.h>

using namespace droppix;

namespace {
// Unique-ish host keys per test so parallel/repeat runs don't collide in the real
// QSettings-backed store, and each test cleans up after itself.
std::string test_host(const char* suffix) {
  return std::string("unit-test-host-") + suffix;
}
}  // namespace

TEST(TlsTrust, UnknownHostIsNotPaired) {
  TlsTrust t;
  const std::string h = test_host("unknown");
  t.clear(h);
  EXPECT_FALSE(t.isPaired(h));
  EXPECT_FALSE(t.pinnedFingerprint(h).has_value());
}

TEST(TlsTrust, PinPersistsAndReadsBack) {
  TlsTrust t;
  const std::string h = test_host("pin");
  t.clear(h);
  t.pin(h, "deadbeef");
  EXPECT_TRUE(t.isPaired(h));
  ASSERT_TRUE(t.pinnedFingerprint(h).has_value());
  EXPECT_EQ(*t.pinnedFingerprint(h), "deadbeef");
  t.clear(h);
}

TEST(TlsTrust, ClearRemovesThePin) {
  TlsTrust t;
  const std::string h = test_host("clear");
  t.pin(h, "abc123");
  ASSERT_TRUE(t.isPaired(h));
  t.clear(h);
  EXPECT_FALSE(t.isPaired(h));
}

TEST(TlsTrust, RepinOverwritesThePreviousFingerprint) {
  // Models the "PC identity changed" case: re-pairing after a cert-changed dialog must
  // replace the old pin, not append/duplicate.
  TlsTrust t;
  const std::string h = test_host("repin");
  t.clear(h);
  t.pin(h, "old-fingerprint");
  t.pin(h, "new-fingerprint");
  ASSERT_TRUE(t.pinnedFingerprint(h).has_value());
  EXPECT_EQ(*t.pinnedFingerprint(h), "new-fingerprint");
  t.clear(h);
}

TEST(TlsTrust, DifferentHostsAreIndependent) {
  TlsTrust t;
  const std::string a = test_host("a"), b = test_host("b");
  t.clear(a); t.clear(b);
  t.pin(a, "fp-a");
  EXPECT_TRUE(t.isPaired(a));
  EXPECT_FALSE(t.isPaired(b));
  t.clear(a);
}

TEST(TlsTrust, CertDerAndFingerprintHandleNullGracefully) {
  EXPECT_TRUE(cert_der(nullptr).empty());
  EXPECT_TRUE(cert_fingerprint(nullptr).empty());
}
