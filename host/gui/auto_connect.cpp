#include "auto_connect.h"
#include <QHash>

namespace droppix {
namespace {
bool is_usb_key(const QString& k) {
  return k.startsWith("usb-aoa:") || k.startsWith("usb:");
}
}  // namespace

AutoConnectPlan devicesToConnect(bool enabled,
                                 const QList<AutoConnectCandidate>& candidates,
                                 const QList<ActiveSessionRef>& active) {
  AutoConnectPlan plan;
  if (!enabled) return plan;

  QSet<QString> activeKeys;
  QSet<QString> usbIds;                 // ids already streaming over the cable (best transport)
  QHash<QString, QString> netKeyById;   // ids streaming over the net -> their session key
  for (const auto& a : active) {
    activeKeys.insert(a.key);
    if (a.id.isEmpty()) continue;
    if (is_usb_key(a.key)) usbIds.insert(a.id);
    else netKeyById.insert(a.id, a.key);
  }

  // USB candidates first: for a tablet visible on both transports, the cable wins.
  QList<AutoConnectCandidate> ordered;
  for (const auto& c : candidates) if (is_usb_key(c.key)) ordered.push_back(c);
  for (const auto& c : candidates) if (!is_usb_key(c.key)) ordered.push_back(c);

  QSet<QString> pickedIds;   // one transport per device id per pass
  for (const auto& c : ordered) {
    if (!c.eligible) continue;
    if (activeKeys.contains(c.key)) continue;
    if (!c.id.isEmpty() && pickedIds.contains(c.id)) continue;
    if (!c.id.isEmpty() && usbIds.contains(c.id)) continue;   // already on the cable
    if (is_usb_key(c.key) && !c.id.isEmpty() && netKeyById.contains(c.id)) {
      // The tablet is on Wi-Fi but its cable just became usable: stop the net session;
      // the follow-up evaluation connects USB once the teardown completes.
      plan.disconnect.push_back(netKeyById.value(c.id));
      pickedIds.insert(c.id);
      continue;
    }
    if (!c.id.isEmpty() && netKeyById.contains(c.id)) continue;  // net candidate, already on net
    plan.connect.push_back(c.key);
    if (!c.id.isEmpty()) pickedIds.insert(c.id);
  }
  return plan;
}

}  // namespace droppix
