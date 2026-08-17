#include "client_theme.h"

#include <QSettings>

namespace droppix {
namespace {
QSettings store() { return QSettings("droppix", "droppix_client"); }
constexpr const char* kKey = "ui/theme";
}  // namespace

Theme loadClientTheme() {
  return store().value(kKey, "dark").toString() == "light" ? Theme::Light : Theme::Dark;
}

void saveClientTheme(Theme t) {
  QSettings s = store();
  s.setValue(kKey, t == Theme::Light ? "light" : "dark");
}

}  // namespace droppix
