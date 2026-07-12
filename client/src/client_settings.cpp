#include "client_settings.h"
#include <QSettings>
namespace droppix {
int rotation_to_code(int d) { switch (d) { case 90: return 1; case 180: return 2; case 270: return 3; default: return 0; } }
ClientSettings ClientSettingsStore::load() {
  QSettings q("droppix", "droppix_client");
  ClientSettings s;
  s.width = q.value("width", 0).toInt();   s.height = q.value("height", 0).toInt();
  s.fps = q.value("fps", 60).toInt();      s.audio = q.value("audio", false).toBool();
  s.rotation = q.value("rotation", 0).toInt();
  s.bitrate_kbps = q.value("bitrate", 8000).toInt();
  s.flip_horizontal = q.value("flip_h", false).toBool();
  return s;
}
void ClientSettingsStore::save(const ClientSettings& s) {
  QSettings q("droppix", "droppix_client");
  q.setValue("width", s.width);   q.setValue("height", s.height);
  q.setValue("fps", s.fps);       q.setValue("audio", s.audio);
  q.setValue("rotation", s.rotation);
  q.setValue("bitrate", s.bitrate_kbps);
  q.setValue("flip_h", s.flip_horizontal);
}
}  // namespace droppix
