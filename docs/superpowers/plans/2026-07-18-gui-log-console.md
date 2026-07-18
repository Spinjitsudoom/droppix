# In-GUI Log Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a searchable, filterable "Debug log" dock panel to `droppix_gui` that captures the streamer's output and the GUI's own Qt log messages, so debugging no longer requires reading the terminal.

**Architecture:** A single in-memory `LogBuffer` (ring, 5000) is fed from two sources — the per-session `StreamController::logLine` (classified into source/level) and a `qInstallMessageHandler` shim that forwards the GUI's own `qInfo`/`qWarning`/`qCritical` while chaining the previous handler so terminal/journald output is preserved. A `LogPanel` (`QDockWidget` at the bottom) shows the buffer through a `LogModel` + `LogFilterProxy` with search/level/source filtering, autoscroll, copy, and save.

**Tech Stack:** C++17, Qt6 (Widgets), GoogleTest, CMake. Builds inside the `droppix-dev` distrobox against build dir `/home/Spinjitsudoomyt/droppix-build`.

## Global Constraints

- C++ standard: **C++17** (`set(CMAKE_CXX_STANDARD 17)`).
- All new source lives under `host/gui/`; tests under `host/gui/tests/`.
- New GUI logic is added to the **`droppix_gui`** target; logic tests to the **`droppix_gui_tests`** target. Do **not** add GUI sources to `droppix_core` or `droppix_tests`.
- `droppix_gui_tests` links only `Qt6::Widgets` + `GTest::gtest_main`; tests must not require `Qt6::Test` (no `QSignalSpy`). Verify signals with a lambda counter connected via a context object.
- Namespace `droppix` for all new symbols.
- Preserve terminal/journald output: the message-handler shim must chain the previously installed handler.
- Ring capacity constant: `LogBuffer::kCap = 5000`.
- Build command (from repo root):
  `distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j'`
- Test command:
  `distrobox enter droppix-dev -- bash -lc 'ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure -R "LogBuffer|LogClassify|LogForwarder|LogFilter"'`
- Commit after each task. Branch: `feat/gui-log-console`.

---

### Task 1: LogEntry value type + streamer-line classifier

**Files:**
- Create: `host/gui/log_entry.h`
- Create: `host/gui/log_classify.h`
- Create: `host/gui/log_classify.cpp`
- Test: `host/gui/tests/test_log_classify.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/log_classify.cpp` to `droppix_gui`; add `gui/log_classify.cpp` + `gui/tests/test_log_classify.cpp` to `droppix_gui_tests`)

**Interfaces:**
- Produces: `struct droppix::LogEntry { qint64 epochMs; QString session; QString source; LogLevel level; QString text; };`, `enum class droppix::LogLevel { Info, Warn, Error };`, `struct droppix::Classified { QString source; LogLevel level; QString text; };`, `Classified droppix::classifyStreamerLine(const QString& raw);`

- [ ] **Step 1: Create `host/gui/log_entry.h`**

```cpp
#pragma once
#include <QString>
#include <QtGlobal>

namespace droppix {

enum class LogLevel { Info, Warn, Error };

struct LogEntry {
  qint64   epochMs = 0;      // GUI receipt time (QDateTime::currentMSecsSinceEpoch)
  QString  session;          // session key, e.g. "usb-aoa:R32..."; empty for GUI-global
  QString  source;           // e.g. "tls", "web", "enc", "gui"; may be empty
  LogLevel level = LogLevel::Info;
  QString  text;             // message body (tag stripped into `source` when parsed)
};

}  // namespace droppix
```

- [ ] **Step 2: Create `host/gui/log_classify.h`**

```cpp
#pragma once
#include <QString>
#include "log_entry.h"

namespace droppix {

struct Classified {
  QString  source;
  LogLevel level = LogLevel::Info;
  QString  text;
};

// Split a raw streamer log line into a source tag (leading "word:" where word
// starts with a lowercase letter), an inferred level (keyword heuristic), and
// the remaining text. Best-effort; the raw text is always preserved.
Classified classifyStreamerLine(const QString& raw);

}  // namespace droppix
```

- [ ] **Step 3: Write the failing test `host/gui/tests/test_log_classify.cpp`**

```cpp
#include "log_classify.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(LogClassify, TaggedErrorLine) {
  const Classified c = classifyStreamerLine("tls: SSL_accept failed");
  EXPECT_EQ(c.source, QStringLiteral("tls"));
  EXPECT_EQ(c.text, QStringLiteral("SSL_accept failed"));
  EXPECT_EQ(c.level, LogLevel::Error);
}

TEST(LogClassify, TaggedInfoLine) {
  const Classified c = classifyStreamerLine("web: websocket client from 192.168.1.5");
  EXPECT_EQ(c.source, QStringLiteral("web"));
  EXPECT_EQ(c.text, QStringLiteral("websocket client from 192.168.1.5"));
  EXPECT_EQ(c.level, LogLevel::Info);
}

TEST(LogClassify, TaggedWarnLine) {
  const Classified c = classifyStreamerLine("vaapi: low_power entrypoint retry");
  EXPECT_EQ(c.source, QStringLiteral("vaapi"));
  EXPECT_EQ(c.level, LogLevel::Warn);
}

TEST(LogClassify, UntaggedLine) {
  const Classified c = classifyStreamerLine("starting encoder");
  EXPECT_TRUE(c.source.isEmpty());
  EXPECT_EQ(c.text, QStringLiteral("starting encoder"));
  EXPECT_EQ(c.level, LogLevel::Info);
}

TEST(LogClassify, TimestampIsNotATag) {
  // A leading digit run must NOT be treated as a source tag.
  const Classified c = classifyStreamerLine("12:04 something happened");
  EXPECT_TRUE(c.source.isEmpty());
  EXPECT_EQ(c.text, QStringLiteral("12:04 something happened"));
}
```

- [ ] **Step 4: Add CMake entries so the test compiles**

In `host/CMakeLists.txt`, inside the `droppix_gui` `target_sources(... PRIVATE` block (the block starting at `gui/profile_store.cpp`), add:

```cmake
    gui/log_classify.cpp
```

In the `droppix_gui_tests` `add_executable(droppix_gui_tests` list, add:

```cmake
    gui/tests/test_log_classify.cpp
    gui/log_classify.cpp
```

- [ ] **Step 5: Run the test to verify it fails (link error, classify not implemented)**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j'
```
Expected: FAIL — `undefined reference to droppix::classifyStreamerLine`.

- [ ] **Step 6: Implement `host/gui/log_classify.cpp`**

```cpp
#include "log_classify.h"
#include <QRegularExpression>

namespace droppix {
namespace {

LogLevel levelForText(const QString& raw) {
  const QString l = raw.toLower();
  static const char* kError[] = {"fail", "error", "errno", "refused", "cannot", "unable"};
  static const char* kWarn[]  = {"warn", "retry", "deprecated"};
  for (const char* w : kError) if (l.contains(QLatin1String(w))) return LogLevel::Error;
  for (const char* w : kWarn)  if (l.contains(QLatin1String(w))) return LogLevel::Warn;
  return LogLevel::Info;
}

}  // namespace

Classified classifyStreamerLine(const QString& raw) {
  // Tag must start with a lowercase letter so "12:04" style prefixes don't match.
  static const QRegularExpression kTag(QStringLiteral("^([a-z][a-z0-9_-]*):\\s*(.*)$"));
  Classified c;
  const QRegularExpressionMatch m = kTag.match(raw);
  if (m.hasMatch()) {
    c.source = m.captured(1);
    c.text = m.captured(2);
  } else {
    c.text = raw;
  }
  c.level = levelForText(raw);
  return c;
}

}  // namespace droppix
```

- [ ] **Step 7: Run the test to verify it passes**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure -R LogClassify'
```
Expected: PASS — 5 LogClassify tests pass.

- [ ] **Step 8: Commit**

```bash
git add host/gui/log_entry.h host/gui/log_classify.h host/gui/log_classify.cpp host/gui/tests/test_log_classify.cpp host/CMakeLists.txt
git commit -m "feat(gui): LogEntry type + streamer log line classifier"
```

---

### Task 2: LogBuffer (ring sink)

**Files:**
- Create: `host/gui/log_buffer.h`
- Create: `host/gui/log_buffer.cpp`
- Test: `host/gui/tests/test_log_buffer.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/log_buffer.cpp` to `droppix_gui`; add `gui/tests/test_log_buffer.cpp` + `gui/log_buffer.cpp` to `droppix_gui_tests`)

**Interfaces:**
- Consumes: `droppix::LogEntry` (Task 1).
- Produces: `class droppix::LogBuffer : QObject` with `static constexpr int kCap = 5000;`, `void append(const LogEntry&)`, `void clear()`, `const std::deque<LogEntry>& entries() const`, signals `entryAdded(const LogEntry&)` and `cleared()`.

- [ ] **Step 1: Create `host/gui/log_buffer.h`**

```cpp
#pragma once
#include <QObject>
#include <deque>
#include "log_entry.h"

namespace droppix {

// The single in-memory sink for all log entries. Ring buffer capped at kCap;
// oldest entries are dropped. GUI-thread affinity: append() is only called on
// the GUI thread (the log forwarder marshals cross-thread messages).
class LogBuffer : public QObject {
  Q_OBJECT
 public:
  static constexpr int kCap = 5000;
  explicit LogBuffer(QObject* parent = nullptr);

  void append(const LogEntry& e);
  void clear();
  const std::deque<LogEntry>& entries() const { return entries_; }

 signals:
  void entryAdded(const LogEntry& e);
  void cleared();

 private:
  std::deque<LogEntry> entries_;
};

}  // namespace droppix
```

- [ ] **Step 2: Write the failing test `host/gui/tests/test_log_buffer.cpp`**

```cpp
#include "log_buffer.h"
#include <gtest/gtest.h>

using namespace droppix;

static LogEntry mk(const QString& t) {
  LogEntry e;
  e.text = t;
  return e;
}

TEST(LogBuffer, AppendEmitsAndStores) {
  LogBuffer buf;
  int count = 0;
  LogEntry last;
  QObject::connect(&buf, &LogBuffer::entryAdded, &buf,
                   [&](const LogEntry& e) { ++count; last = e; });
  buf.append(mk("hello"));
  EXPECT_EQ(count, 1);
  EXPECT_EQ(last.text, QStringLiteral("hello"));
  EXPECT_EQ(buf.entries().size(), static_cast<size_t>(1));
}

TEST(LogBuffer, RingCapDropsOldest) {
  LogBuffer buf;
  for (int i = 0; i < LogBuffer::kCap + 10; ++i) buf.append(mk(QString::number(i)));
  EXPECT_EQ(static_cast<int>(buf.entries().size()), LogBuffer::kCap);
  EXPECT_EQ(buf.entries().front().text, QString::number(10));            // 0..9 dropped
  EXPECT_EQ(buf.entries().back().text, QString::number(LogBuffer::kCap + 9));
}

TEST(LogBuffer, ClearEmpties) {
  LogBuffer buf;
  buf.append(mk("x"));
  int cleared = 0;
  QObject::connect(&buf, &LogBuffer::cleared, &buf, [&] { ++cleared; });
  buf.clear();
  EXPECT_TRUE(buf.entries().empty());
  EXPECT_EQ(cleared, 1);
}
```

- [ ] **Step 3: Add CMake entries**

In `host/CMakeLists.txt`, add to the `droppix_gui` `target_sources` block:

```cmake
    gui/log_buffer.cpp
```

Add to the `droppix_gui_tests` `add_executable` list:

```cmake
    gui/tests/test_log_buffer.cpp
    gui/log_buffer.cpp
```

- [ ] **Step 4: Run the test to verify it fails**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j'
```
Expected: FAIL — `undefined reference to droppix::LogBuffer::append` / `LogBuffer::clear` / constructor.

- [ ] **Step 5: Implement `host/gui/log_buffer.cpp`**

```cpp
#include "log_buffer.h"

namespace droppix {

LogBuffer::LogBuffer(QObject* parent) : QObject(parent) {}

void LogBuffer::append(const LogEntry& e) {
  entries_.push_back(e);
  while (static_cast<int>(entries_.size()) > kCap) entries_.pop_front();
  emit entryAdded(e);
}

void LogBuffer::clear() {
  entries_.clear();
  emit cleared();
}

}  // namespace droppix
```

- [ ] **Step 6: Run the test to verify it passes**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure -R LogBuffer'
```
Expected: PASS — 3 LogBuffer tests pass.

- [ ] **Step 7: Commit**

```bash
git add host/gui/log_buffer.h host/gui/log_buffer.cpp host/gui/tests/test_log_buffer.cpp host/CMakeLists.txt
git commit -m "feat(gui): LogBuffer ring sink for log entries"
```

---

### Task 3: Qt message-handler forwarder

**Files:**
- Create: `host/gui/log_forwarder.h`
- Create: `host/gui/log_forwarder.cpp`
- Test: `host/gui/tests/test_log_forwarder.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/log_forwarder.cpp` to `droppix_gui`; add `gui/tests/test_log_forwarder.cpp` + `gui/log_forwarder.cpp` to `droppix_gui_tests`)

**Interfaces:**
- Consumes: `droppix::LogBuffer` (Task 2), `droppix::LogLevel` (Task 1).
- Produces: `LogLevel droppix::levelForMsgType(QtMsgType);`, `void droppix::installLogForwarder(LogBuffer* buffer);`

- [ ] **Step 1: Create `host/gui/log_forwarder.h`**

```cpp
#pragma once
#include <QtGlobal>
#include "log_entry.h"

namespace droppix {

class LogBuffer;

// Map a Qt message type to our level.
LogLevel levelForMsgType(QtMsgType t);

// Install a qInstallMessageHandler shim that forwards every Qt log message
// (qDebug/qInfo/qWarning/qCritical/qFatal) into `buffer` on the GUI thread,
// while chaining the previously installed handler so terminal/journald output
// is preserved. Call once, after QApplication is constructed.
void installLogForwarder(LogBuffer* buffer);

}  // namespace droppix
```

- [ ] **Step 2: Write the failing test `host/gui/tests/test_log_forwarder.cpp`**

```cpp
#include "log_forwarder.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(LogForwarder, LevelMapping) {
  EXPECT_EQ(levelForMsgType(QtDebugMsg), LogLevel::Info);
  EXPECT_EQ(levelForMsgType(QtInfoMsg), LogLevel::Info);
  EXPECT_EQ(levelForMsgType(QtWarningMsg), LogLevel::Warn);
  EXPECT_EQ(levelForMsgType(QtCriticalMsg), LogLevel::Error);
  EXPECT_EQ(levelForMsgType(QtFatalMsg), LogLevel::Error);
}
```

- [ ] **Step 3: Add CMake entries**

In `host/CMakeLists.txt`, add to the `droppix_gui` `target_sources` block:

```cmake
    gui/log_forwarder.cpp
```

Add to the `droppix_gui_tests` `add_executable` list:

```cmake
    gui/tests/test_log_forwarder.cpp
    gui/log_forwarder.cpp
```

(Task 2 already added `gui/log_buffer.cpp` to `droppix_gui_tests`, which `log_forwarder.cpp` depends on.)

- [ ] **Step 4: Run the test to verify it fails**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j'
```
Expected: FAIL — `undefined reference to droppix::levelForMsgType`.

- [ ] **Step 5: Implement `host/gui/log_forwarder.cpp`**

```cpp
#include "log_forwarder.h"
#include "log_buffer.h"
#include <QDateTime>
#include <QMetaObject>
#include <QString>

namespace droppix {
namespace {

LogBuffer*       g_buffer = nullptr;
QtMessageHandler g_previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  if (g_previous) g_previous(type, ctx, msg);   // keep terminal/journald output
  LogBuffer* buf = g_buffer;
  if (!buf) return;
  LogEntry e;
  e.epochMs = QDateTime::currentMSecsSinceEpoch();
  e.source = QStringLiteral("gui");
  e.level = levelForMsgType(type);
  e.text = msg;
  // Qt messages may originate on any thread; marshal onto the buffer's (GUI) thread.
  QMetaObject::invokeMethod(buf, [buf, e] { buf->append(e); }, Qt::QueuedConnection);
}

}  // namespace

LogLevel levelForMsgType(QtMsgType t) {
  switch (t) {
    case QtWarningMsg:  return LogLevel::Warn;
    case QtCriticalMsg:
    case QtFatalMsg:    return LogLevel::Error;
    case QtDebugMsg:
    case QtInfoMsg:
    default:            return LogLevel::Info;
  }
}

void installLogForwarder(LogBuffer* buffer) {
  g_buffer = buffer;
  g_previous = qInstallMessageHandler(handler);
}

}  // namespace droppix
```

- [ ] **Step 6: Run the test to verify it passes**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure -R LogForwarder'
```
Expected: PASS — 1 LogForwarder test passes.

- [ ] **Step 7: Commit**

```bash
git add host/gui/log_forwarder.h host/gui/log_forwarder.cpp host/gui/tests/test_log_forwarder.cpp host/CMakeLists.txt
git commit -m "feat(gui): qInstallMessageHandler forwarder into LogBuffer"
```

---

### Task 4: LogModel + LogFilterProxy (view backing + filtering)

**Files:**
- Create: `host/gui/log_model.h`
- Create: `host/gui/log_model.cpp`
- Test: `host/gui/tests/test_log_filter.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/log_model.cpp` to `droppix_gui`; add `gui/tests/test_log_filter.cpp` + `gui/log_model.cpp` to `droppix_gui_tests`)

**Interfaces:**
- Consumes: `droppix::LogBuffer` (Task 2), `droppix::LogEntry`/`LogLevel` (Task 1).
- Produces: `class droppix::LogModel : QAbstractListModel` with role enum `{ LevelRole = Qt::UserRole+1, SourceRole, SessionRole, TextRole }` and ctor `LogModel(LogBuffer*, QObject*)`; `class droppix::LogFilterProxy : QSortFilterProxyModel` with `void setSearchText(const QString&)`, `void setLevelEnabled(LogLevel, bool)`, `void setSourceFilter(const QString&)`.

- [ ] **Step 1: Create `host/gui/log_model.h`**

```cpp
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
```

- [ ] **Step 2: Write the failing test `host/gui/tests/test_log_filter.cpp`**

```cpp
#include "log_buffer.h"
#include "log_model.h"
#include <gtest/gtest.h>

using namespace droppix;

static LogEntry mk(LogLevel lvl, const QString& src, const QString& text) {
  LogEntry e;
  e.level = lvl;
  e.source = src;
  e.text = text;
  return e;
}

TEST(LogFilter, LevelAndSearchAndSource) {
  LogBuffer buf;
  buf.append(mk(LogLevel::Info,  "web", "websocket client connected"));
  buf.append(mk(LogLevel::Error, "tls", "SSL_accept failed"));
  buf.append(mk(LogLevel::Info,  "enc", "vaapi ready"));

  LogModel model(&buf);
  LogFilterProxy proxy;
  proxy.setSourceModel(&model);
  EXPECT_EQ(proxy.rowCount(), 3);

  // Disable Info -> only the Error row remains.
  proxy.setLevelEnabled(LogLevel::Info, false);
  EXPECT_EQ(proxy.rowCount(), 1);
  proxy.setLevelEnabled(LogLevel::Info, true);
  EXPECT_EQ(proxy.rowCount(), 3);

  // Substring search over text.
  proxy.setSearchText("client");
  EXPECT_EQ(proxy.rowCount(), 1);
  proxy.setSearchText("");
  EXPECT_EQ(proxy.rowCount(), 3);

  // Exact source filter.
  proxy.setSourceFilter("tls");
  EXPECT_EQ(proxy.rowCount(), 1);
  proxy.setSourceFilter("");
  EXPECT_EQ(proxy.rowCount(), 3);
}

TEST(LogFilter, BackfillReflectsExistingEntries) {
  LogBuffer buf;
  buf.append(mk(LogLevel::Info, "web", "one"));
  buf.append(mk(LogLevel::Info, "web", "two"));
  LogModel model(&buf);   // constructed AFTER entries exist
  EXPECT_EQ(model.rowCount(), 2);
}
```

- [ ] **Step 3: Add CMake entries**

In `host/CMakeLists.txt`, add to the `droppix_gui` `target_sources` block:

```cmake
    gui/log_model.cpp
```

Add to the `droppix_gui_tests` `add_executable` list:

```cmake
    gui/tests/test_log_filter.cpp
    gui/log_model.cpp
```

- [ ] **Step 4: Run the test to verify it fails**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j'
```
Expected: FAIL — `undefined reference` to `LogModel` / `LogFilterProxy` members.

- [ ] **Step 5: Implement `host/gui/log_model.cpp`**

```cpp
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
```

- [ ] **Step 6: Run the test to verify it passes**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui_tests -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure -R LogFilter'
```
Expected: PASS — 2 LogFilter tests pass.

- [ ] **Step 7: Commit**

```bash
git add host/gui/log_model.h host/gui/log_model.cpp host/gui/tests/test_log_filter.cpp host/CMakeLists.txt
git commit -m "feat(gui): LogModel + LogFilterProxy for the log view"
```

---

### Task 5: LogPanel dock widget

**Files:**
- Create: `host/gui/log_panel.h`
- Create: `host/gui/log_panel.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/log_panel.cpp` to `droppix_gui` only)

**Interfaces:**
- Consumes: `droppix::LogBuffer` (Task 2), `droppix::LogModel`/`LogFilterProxy` (Task 4), `droppix::LogLevel` (Task 1).
- Produces: `class droppix::LogPanel : QDockWidget` with ctor `LogPanel(LogBuffer* buffer, QWidget* parent = nullptr)`, `objectName() == "logPanel"`.

No unit test — the panel is a thin view over already-tested logic. Verified manually in Task 6.

- [ ] **Step 1: Create `host/gui/log_panel.h`**

```cpp
#pragma once
#include <QDockWidget>

class QLineEdit;
class QListView;
class QComboBox;
class QCheckBox;

namespace droppix {

class LogBuffer;
class LogModel;
class LogFilterProxy;

// Bottom dock panel showing the LogBuffer with search / level / source filters,
// autoscroll, copy, clear, and save-to-file.
class LogPanel : public QDockWidget {
  Q_OBJECT
 public:
  explicit LogPanel(LogBuffer* buffer, QWidget* parent = nullptr);

 private:
  void refreshSources();
  void copySelection();
  void saveToFile();

  LogBuffer*      buffer_;
  LogModel*       model_;
  LogFilterProxy* proxy_;
  QListView*      view_;
  QLineEdit*      search_;
  QComboBox*      sourceBox_;
  QCheckBox*      autoscroll_;
};

}  // namespace droppix
```

- [ ] **Step 2: Create `host/gui/log_panel.cpp`**

```cpp
#include "log_panel.h"
#include "log_buffer.h"
#include "log_model.h"
#include "log_entry.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QScrollBar>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace droppix {

LogPanel::LogPanel(LogBuffer* buffer, QWidget* parent)
    : QDockWidget(tr("Debug log"), parent), buffer_(buffer) {
  setObjectName(QStringLiteral("logPanel"));

  auto* root = new QWidget(this);
  auto* col = new QVBoxLayout(root);
  col->setContentsMargins(4, 4, 4, 4);

  // toolbar row
  auto* bar = new QHBoxLayout();
  search_ = new QLineEdit(root);
  search_->setPlaceholderText(tr("search…"));
  bar->addWidget(search_, 1);

  auto* infoBtn = new QToolButton(root);
  infoBtn->setText(QStringLiteral("INF")); infoBtn->setCheckable(true); infoBtn->setChecked(true);
  auto* warnBtn = new QToolButton(root);
  warnBtn->setText(QStringLiteral("WRN")); warnBtn->setCheckable(true); warnBtn->setChecked(true);
  auto* errBtn = new QToolButton(root);
  errBtn->setText(QStringLiteral("ERR")); errBtn->setCheckable(true); errBtn->setChecked(true);
  bar->addWidget(infoBtn); bar->addWidget(warnBtn); bar->addWidget(errBtn);

  sourceBox_ = new QComboBox(root);
  sourceBox_->addItem(tr("all sources"), QString());
  bar->addWidget(sourceBox_);

  autoscroll_ = new QCheckBox(tr("autoscroll"), root);
  autoscroll_->setChecked(true);
  bar->addWidget(autoscroll_);

  auto* clearBtn = new QToolButton(root); clearBtn->setText(tr("Clear"));
  auto* copyBtn  = new QToolButton(root); copyBtn->setText(tr("Copy"));
  auto* saveBtn  = new QToolButton(root); saveBtn->setText(tr("Save…"));
  bar->addWidget(clearBtn); bar->addWidget(copyBtn); bar->addWidget(saveBtn);
  col->addLayout(bar);

  // view
  model_ = new LogModel(buffer_, this);
  proxy_ = new LogFilterProxy(this);
  proxy_->setSourceModel(model_);
  view_ = new QListView(root);
  view_->setModel(proxy_);
  view_->setUniformItemSizes(true);
  view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  QFont mono(QStringLiteral("monospace"));
  mono.setStyleHint(QFont::Monospace);
  view_->setFont(mono);
  col->addWidget(view_, 1);

  setWidget(root);

  // wiring
  connect(search_, &QLineEdit::textChanged, proxy_, &LogFilterProxy::setSearchText);
  connect(infoBtn, &QToolButton::toggled, this, [this](bool on) { proxy_->setLevelEnabled(LogLevel::Info, on); });
  connect(warnBtn, &QToolButton::toggled, this, [this](bool on) { proxy_->setLevelEnabled(LogLevel::Warn, on); });
  connect(errBtn,  &QToolButton::toggled, this, [this](bool on) { proxy_->setLevelEnabled(LogLevel::Error, on); });
  connect(sourceBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { proxy_->setSourceFilter(sourceBox_->currentData().toString()); });
  connect(clearBtn, &QToolButton::clicked, buffer_, &LogBuffer::clear);
  connect(copyBtn,  &QToolButton::clicked, this, &LogPanel::copySelection);
  connect(saveBtn,  &QToolButton::clicked, this, &LogPanel::saveToFile);

  // autoscroll: follow the tail; pause when scrolled up; resume at the bottom
  connect(model_, &QAbstractItemModel::rowsInserted, this, [this] {
    if (autoscroll_->isChecked()) view_->scrollToBottom();
  });
  connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
    autoscroll_->setChecked(v == view_->verticalScrollBar()->maximum());
  });

  // keep the source dropdown current as new sources appear
  connect(buffer_, &LogBuffer::entryAdded, this, [this](const LogEntry& e) {
    if (!e.source.isEmpty() && sourceBox_->findData(e.source) < 0)
      sourceBox_->addItem(e.source, e.source);
  });
  refreshSources();
}

void LogPanel::refreshSources() {
  for (const auto& e : buffer_->entries())
    if (!e.source.isEmpty() && sourceBox_->findData(e.source) < 0)
      sourceBox_->addItem(e.source, e.source);
}

void LogPanel::copySelection() {
  const QModelIndexList sel = view_->selectionModel()->selectedIndexes();
  QStringList lines;
  for (const QModelIndex& i : sel) lines << i.data(Qt::DisplayRole).toString();
  if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join('\n'));
}

void LogPanel::saveToFile() {
  const QString suggested =
      QStringLiteral("droppix-%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
  const QString path = QFileDialog::getSaveFileName(this, tr("Save log"), suggested,
                                                    tr("Log files (*.log);;All files (*)"));
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
  QTextStream out(&f);
  for (const auto& e : buffer_->entries()) {
    const QString ts = QDateTime::fromMSecsSinceEpoch(e.epochMs).toString(Qt::ISODate);
    out << ts << ' '
        << (e.session.isEmpty() ? QString() : "[" + e.session + "]")
        << (e.source.isEmpty() ? QString() : "[" + e.source + "]") << ' '
        << e.text << '\n';
  }
}

}  // namespace droppix
```

- [ ] **Step 3: Add CMake entry**

In `host/CMakeLists.txt`, add to the `droppix_gui` `target_sources` block only:

```cmake
    gui/log_panel.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build --target droppix_gui -j'
```
Expected: PASS — `droppix_gui` links (panel not shown yet; wired in Task 6).

- [ ] **Step 5: Commit**

```bash
git add host/gui/log_panel.h host/gui/log_panel.cpp host/CMakeLists.txt
git commit -m "feat(gui): LogPanel dock widget (view, filters, autoscroll, save)"
```

---

### Task 6: Wire the console into the application

**Files:**
- Modify: `host/gui/main_window.h` (add members + `logEvent` helper)
- Modify: `host/gui/main_window.cpp` (create buffer, install forwarder, add dock + toggle, reroute `wireSession`)

**Interfaces:**
- Consumes: `installLogForwarder` (Task 3), `LogBuffer` (Task 2), `LogPanel` (Task 5), `classifyStreamerLine`/`Classified`/`LogEntry`/`LogLevel` (Tasks 1, 4).
- Produces: no new public API; behavior change only.

- [ ] **Step 1: Add members + helper to `host/gui/main_window.h`**

Add these includes near the other GUI includes (after `#include "audio_sink.h"`):

```cpp
#include "log_buffer.h"
#include "log_panel.h"
#include "log_entry.h"
```

In the `private:` section, near `void showPairingPopup(...)`, add the helper declaration:

```cpp
  void logEvent(const QString& key, const QString& source, LogLevel level, const QString& text);
```

In the members area (after `SessionManager sessions_;`), add:

```cpp
  LogBuffer* logBuffer_ = nullptr;   // app-wide log sink (streamer + GUI messages)
  LogPanel*  logPanel_ = nullptr;    // bottom "Debug log" dock
```

- [ ] **Step 2: In `host/gui/main_window.cpp`, add includes**

At the top of the file with the other includes, add:

```cpp
#include "log_forwarder.h"
#include "log_classify.h"
#include <QDateTime>
#include <QMenuBar>
#include <QAction>
#include <QKeySequence>
```

- [ ] **Step 3: Create the buffer + dock in the constructor**

In `MainWindow::MainWindow(...)`, immediately after the `setCentralWidget(central);` line (around line 261), add:

```cpp
  // --- Debug log console ---
  logBuffer_ = new LogBuffer(this);
  installLogForwarder(logBuffer_);            // capture GUI qInfo/qWarning/qCritical too
  logPanel_ = new LogPanel(logBuffer_, this);
  addDockWidget(Qt::BottomDockWidgetArea, logPanel_);
  logPanel_->hide();                          // start hidden; toggle with the action below
  QAction* toggleLog = logPanel_->toggleViewAction();
  toggleLog->setText(tr("&Debug log"));
  toggleLog->setShortcut(QKeySequence(Qt::Key_F12));
  addAction(toggleLog);                       // make F12 work window-wide
  menuBar()->addMenu(tr("&View"))->addAction(toggleLog);
```

- [ ] **Step 4: Add the `logEvent` helper implementation**

Anywhere in `main_window.cpp` at namespace/member scope (e.g. just above `MainWindow::wireSession`), add:

```cpp
void MainWindow::logEvent(const QString& key, const QString& source, LogLevel level, const QString& text) {
  if (!logBuffer_) return;
  LogEntry e;
  e.epochMs = QDateTime::currentMSecsSinceEpoch();
  e.session = key;
  e.source = source;
  e.level = level;
  e.text = text;
  logBuffer_->append(e);
}
```

- [ ] **Step 5: Reroute `wireSession` from the terminal into the buffer**

In `MainWindow::wireSession(StreamController* c, const QString& key)`, replace this line:

```cpp
  connect(c, &StreamController::logLine, this, [](const QString& l){ qInfo("%s", qUtf8Printable(l)); });
```

with:

```cpp
  connect(c, &StreamController::logLine, this, [this, key](const QString& l){
    const Classified cl = classifyStreamerLine(l);
    logEvent(key, cl.source, cl.level, cl.text);
  });
```

Then update the `connecting` connection in the same function, replacing:

```cpp
  connect(c, &StreamController::connecting, this, [this](const QString& ip){ showPairingPopup(ip); });
```

with:

```cpp
  connect(c, &StreamController::connecting, this, [this, key](const QString& ip){
    logEvent(key, QStringLiteral("conn"), LogLevel::Info, QStringLiteral("client connecting ip=%1").arg(ip));
    showPairingPopup(ip);
  });
```

And in the `approvalRequested` connection, change its capture list from `[this, c]` to `[this, c, key]` and add a log line as the first statement of the lambda body:

```cpp
      logEvent(key, QStringLiteral("pair"), LogLevel::Info,
               QStringLiteral("approval requested id=%1 name=%2 ip=%3").arg(id, name, ip));
```

- [ ] **Step 6: Build the whole GUI**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build -j'
```
Expected: PASS — all targets build, including `droppix_gui`.

- [ ] **Step 7: Run the full test suite (no regressions)**

Run:
```bash
distrobox enter droppix-dev -- bash -lc 'ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'
```
Expected: PASS — the pre-existing 222 tests plus the new Log* tests all pass.

- [ ] **Step 8: Manual verification (host, with display)**

1. Launch: `distrobox enter droppix-dev -- bash -lc '/home/Spinjitsudoomyt/droppix-build/droppix_gui'`
2. Press **F12** (or View → Debug log) — the bottom "Debug log" dock appears.
3. Start a session and connect a client (or trigger a TLS/pkexec error) — confirm streamer lines appear with `[session][source]` tags and level coloring (ERR red, WRN amber).
4. Type in **search**, toggle **INF/WRN/ERR**, pick a **source** — confirm filtering works.
5. Click **Save…**, write a `.log`, confirm it contains the full buffer.
6. Confirm the **terminal still prints** the same lines (journald/terminal preserved).

- [ ] **Step 9: Commit**

```bash
git add host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(gui): wire Debug log console into MainWindow (F12 dock)"
```

---

## Notes on execution order

Tasks 1→4 are pure logic with unit tests and can each be verified in isolation. Task 5 (widget) compiles against Task 4. Task 6 wires everything and is verified by the full build + test suite + a manual smoke test. Keep commits per task.
