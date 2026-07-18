#include "log_model.h"
#include "log_buffer.h"
#include <QColor>
#include <QDateTime>

namespace droppix {
namespace {

QColor colorFor(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return QColor(0xff, 0x6b, 0x6b);
    case LogLevel::Warn:  return QColor(0xff, 0xc1, 0x07);
    case LogLevel::Info:
    default:              return QColor();   // invalid => let the theme decide
  }
}

QString levelTag(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return QStringLiteral("ERR");
    case LogLevel::Warn:  return QStringLiteral("WRN");
    case LogLevel::Info:
    default:              return QStringLiteral("INF");
  }
}

}  // namespace

LogModel::LogModel(LogBuffer* buffer, QObject* parent) : QAbstractListModel(parent) {
  for (const auto& e : buffer->entries()) rows_.append(e);   // backfill existing
  connect(buffer, &LogBuffer::entryAdded, this, &LogModel::onEntryAdded);
  connect(buffer, &LogBuffer::cleared, this, &LogModel::onCleared);
}

int LogModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : rows_.size();
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
  const LogEntry& e = rows_.at(index.row());
  switch (role) {
    case Qt::DisplayRole: {
      const QString ts = QDateTime::fromMSecsSinceEpoch(e.epochMs).toString("HH:mm:ss");
      QString tag;
      if (!e.session.isEmpty()) tag += "[" + e.session + "]";
      if (!e.source.isEmpty())  tag += "[" + e.source + "]";
      return QStringLiteral("%1 %2 %3 %4").arg(ts, levelTag(e.level), tag, e.text);
    }
    case Qt::ForegroundRole: {
      const QColor c = colorFor(e.level);
      return c.isValid() ? QVariant(c) : QVariant();
    }
    case LevelRole:   return static_cast<int>(e.level);
    case SourceRole:  return e.source;
    case SessionRole: return e.session;
    case TextRole:    return e.text;
    default:          return {};
  }
}

void LogModel::onEntryAdded(const LogEntry& e) {
  beginInsertRows(QModelIndex(), rows_.size(), rows_.size());
  rows_.append(e);
  endInsertRows();
  if (rows_.size() > LogBuffer::kCap) {
    beginRemoveRows(QModelIndex(), 0, 0);
    rows_.removeFirst();
    endRemoveRows();
  }
}

void LogModel::onCleared() {
  beginResetModel();
  rows_.clear();
  endResetModel();
}

void LogFilterProxy::setSearchText(const QString& s) { search_ = s; invalidateFilter(); }

void LogFilterProxy::setLevelEnabled(LogLevel level, bool on) {
  levelOn_[static_cast<int>(level)] = on;
  invalidateFilter();
}

void LogFilterProxy::setSourceFilter(const QString& source) { source_ = source; invalidateFilter(); }

bool LogFilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const {
  const QModelIndex idx = sourceModel()->index(row, 0, parent);
  const int lvl = idx.data(LogModel::LevelRole).toInt();
  if (lvl >= 0 && lvl < 3 && !levelOn_[lvl]) return false;
  if (!source_.isEmpty() && idx.data(LogModel::SourceRole).toString() != source_) return false;
  if (!search_.isEmpty()) {
    const QString text = idx.data(LogModel::TextRole).toString();
    const QString src = idx.data(LogModel::SourceRole).toString();
    if (!text.contains(search_, Qt::CaseInsensitive) && !src.contains(search_, Qt::CaseInsensitive))
      return false;
  }
  return true;
}

}  // namespace droppix
