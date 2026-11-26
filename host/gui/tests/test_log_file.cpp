#include "log_file.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace droppix;

static LogEntry entry(const QString& text, const QString& session = {}, const QString& source = {}) {
  LogEntry e; e.epochMs = 1764115200000; e.session = session; e.source = source; e.text = text;
  return e;
}

// The same format the F12 panel's "Save log" writes, so both read alike.
TEST(LogFile, FormatsTimestampSessionSourceAndText) {
  const QString line = LogFile::format(entry("client HELLO v6", "usb-aoa:R32", "conn"));
  EXPECT_TRUE(line.endsWith(" [usb-aoa:R32][conn] client HELLO v6"));
  EXPECT_TRUE(line.section(' ', 0, 0).contains('T'));          // ISO timestamp first
  EXPECT_TRUE(LogFile::format(entry("plain")).endsWith("  plain"));  // no tags: empty slot
}

TEST(LogFile, AppendsLinesAndCreatesTheDirectory) {
  QTemporaryDir tmp;
  const QString path = tmp.path() + "/sub/droppix.log";
  LogFile f(path, 1 << 20);
  f.write(entry("one"));
  f.write(entry("two"));
  QFile r(path);
  ASSERT_TRUE(r.open(QIODevice::ReadOnly));
  const QString all = QString::fromUtf8(r.readAll());
  EXPECT_TRUE(all.contains(" one\n"));
  EXPECT_TRUE(all.contains(" two\n"));
}

// A long session must not fill the disk: past the cap the file moves to .1 (one generation).
TEST(LogFile, RotatesPastTheSizeCap) {
  QTemporaryDir tmp;
  const QString path = tmp.path() + "/droppix.log";
  LogFile f(path, 200);
  for (int i = 0; i < 20; ++i) f.write(entry(QString("line %1 with some padding text").arg(i)));
  ASSERT_TRUE(QFileInfo::exists(path + ".1"));
  EXPECT_LE(QFileInfo(path).size(), 200 + 100);
  EXPECT_FALSE(QFileInfo::exists(path + ".2"));
  QFile r(path); ASSERT_TRUE(r.open(QIODevice::ReadOnly));
  EXPECT_TRUE(QString::fromUtf8(r.readAll()).contains("line 19"));
}
