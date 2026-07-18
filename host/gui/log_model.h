#pragma once
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QList>
#include "log_entry.h"

namespace droppix {

class LogBuffer;

// Read model over a LogBuffer: backfills existing entries, then appends on
// entryAdded and mirrors the ring cap. Exposes filter roles for the proxy.
class LogModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles {
    LevelRole = Qt::UserRole + 1,
    SourceRole,
    SessionRole,
    TextRole,
  };
  explicit LogModel(LogBuffer* buffer, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;

 private:
  void onEntryAdded(const LogEntry& e);
  void onCleared();
  QList<LogEntry> rows_;
};

// Filters rows by search text (substring over text+source), enabled levels,
// and an optional exact source match.
class LogFilterProxy : public QSortFilterProxyModel {
  Q_OBJECT
 public:
  using QSortFilterProxyModel::QSortFilterProxyModel;
  void setSearchText(const QString& s);
  void setLevelEnabled(LogLevel level, bool on);
  void setSourceFilter(const QString& source);   // empty = all sources

 protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

 private:
  QString search_;
  QString source_;
  bool    levelOn_[3] = {true, true, true};   // indexed by static_cast<int>(LogLevel)
};

}  // namespace droppix
