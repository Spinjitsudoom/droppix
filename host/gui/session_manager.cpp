#include "session_manager.h"

namespace droppix {

bool SessionManager::has(const QString& key) const {
  for (const auto& s : sessions_) if (s.key == key) return true;
  return false;
}

Session* SessionManager::find(const QString& key) {
  for (auto& s : sessions_) if (s.key == key) return &s;
  return nullptr;
}

std::set<int> SessionManager::usedPorts() const {
  std::set<int> p;
  for (const auto& s : sessions_) p.insert(s.port);
  return p;
}

QSet<QString> SessionManager::keys() const {
  QSet<QString> k;
  for (const auto& s : sessions_) k.insert(s.key);
  return k;
}

Session& SessionManager::add(const Session& s) {
  sessions_.append(s);
  return sessions_.last();
}

void SessionManager::remove(const QString& key) {
  for (int i = 0; i < sessions_.size(); ++i)
    if (sessions_[i].key == key) { sessions_.removeAt(i); return; }
}

}  // namespace droppix
