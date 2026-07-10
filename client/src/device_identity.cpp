#include "device_identity.h"
#include <QSettings>
#include <QSysInfo>
#include <QUuid>
#include <QHostInfo>

namespace droppix {
namespace DeviceIdentity {

std::string displayName() {
  QString name = QHostInfo::localHostName();
  if (name.isEmpty()) name = QSysInfo::prettyProductName();
  return name.toStdString();
}

std::string stableId() {
  QSettings s("droppix", "droppix_client");
  QString id = s.value("device_id").toString();
  if (id.isEmpty()) {
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s.setValue("device_id", id);
  }
  return id.toStdString();
}

}  // namespace DeviceIdentity
}  // namespace droppix
