#pragma once
#include <QSet>
#include <QString>
#include <QHash>

namespace droppix {

// Decides when a USB tablet with a live session has actually been unplugged.
//
// The GUI scans attached devices every couple of seconds, and a streaming tablet stays
// visible because the scanner lists accessory-mode devices too. So "its serial vanished"
// is the unplug signal — but it cannot be trusted on a single scan:
//
//  - switching a device INTO accessory mode makes it re-enumerate under a different
//    VID/PID, so it is genuinely absent from sysfs for a moment during normal startup;
//  - a marginal cable drops and re-enumerates the device repeatedly (we have one here that
//    does exactly that), and tearing the monitor down on every blip would be worse than
//    useless.
//
// So require the serial to be missing from several CONSECUTIVE scans before calling it.
// Wrong in one direction costs a few seconds of delay; wrong in the other kills a working
// monitor, which is why this errs toward waiting.
class AoaPresence {
 public:
  explicit AoaPresence(int misses_before_gone = 3) : limit_(misses_before_gone) {}

  // Feed one scan: which serials have live sessions, and which are currently attached.
  // Returns the serials that have now been absent long enough to call unplugged. A serial
  // is reported at most once — it is dropped from tracking as it is reported, so a caller
  // that tears the session down is not told again on the next scan.
  QSet<QString> update(const QSet<QString>& tracked, const QSet<QString>& attached) {
    QSet<QString> gone;
    // Stop tracking serials whose session has ended, so a later reconnect starts clean.
    for (auto it = misses_.begin(); it != misses_.end();)
      it = tracked.contains(it.key()) ? std::next(it) : misses_.erase(it);

    for (const QString& serial : tracked) {
      if (attached.contains(serial)) { misses_.remove(serial); continue; }
      const int n = misses_.value(serial, 0) + 1;
      if (n >= limit_) { misses_.remove(serial); gone.insert(serial); }
      else misses_.insert(serial, n);
    }
    return gone;
  }

  // Consecutive misses recorded for a serial (0 when present or untracked). Test seam.
  int misses(const QString& serial) const { return misses_.value(serial, 0); }

 private:
  int limit_;
  QHash<QString, int> misses_;
};

}  // namespace droppix
