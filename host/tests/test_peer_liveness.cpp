#include "peer_liveness.h"
#include <gtest/gtest.h>

using droppix::PeerLiveness;

// On the real Nexus 10 the app closed its end of the USB link when it went to the background;
// the PC's USB reads just timed out ("no data"), a static screen sent no frames, so the session
// never ended and the tablet could never reopen the accessory. Clients that ping every second
// are now declared gone after a silence.
TEST(PeerLiveness, SilentAfterTheLimitOnceTheClientHasPinged) {
  PeerLiveness l;
  l.heard(1000, /*ping=*/true);
  EXPECT_FALSE(l.silent(5999, 5000));
  EXPECT_TRUE(l.silent(6000, 5000));
}
TEST(PeerLiveness, AnyMessageCountsAsLife) {
  PeerLiveness l;
  l.heard(1000, true);
  l.heard(5500, /*ping=*/false);   // e.g. a touch
  EXPECT_FALSE(l.silent(9000, 5000));
}
TEST(PeerLiveness, NeverArmedForAClientThatDoesNotPing) {
  // The Linux desktop client sends no PINGs: never time it out.
  PeerLiveness l;
  l.heard(1000, false);
  EXPECT_FALSE(l.silent(600000, 5000));
}
TEST(PeerLiveness, ResetForANewSession) {
  PeerLiveness l;
  l.heard(1000, true);
  l.reset();
  EXPECT_FALSE(l.silent(100000, 5000));
}
