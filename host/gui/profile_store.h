#pragma once
#include <QString>
#include <QStringList>
#include "settings.h"

namespace droppix {
// Named Settings profiles persisted to <dir>/profiles.json.
class ProfileStore {
 public:
  explicit ProfileStore(QString dir);
  QStringList names() const;
  bool save(const QString& name, const Settings& s);
  bool load(const QString& name, Settings& out) const;
  bool remove(const QString& name);
  // Remember which profile was last in use so it can be restored on next launch.
  void setLastUsed(const QString& name);
  QString lastUsed() const;

  // The working settings, independent of any named profile.
  //
  // Profiles are an explicit Save; without this, anything changed after the last Save was
  // lost on exit — tick "Web client", close, and it came back unticked. Kept in its own file
  // so restoring how you left the app never rewrites a profile you did not save.
  bool saveSession(const Settings& s);
  bool loadSession(Settings& out) const;

 private:
  QString path() const;          // <dir>/profiles.json
  QString lastUsedPath() const;  // <dir>/last_profile
  QString sessionPath() const;   // <dir>/session_settings.json
  QString dir_;
};
}  // namespace droppix
