#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <string>
#include "main_window.h"
#include "style.h"

namespace {
// Builds before the app name was pinned kept their config in ~/.config/<file>.AppImage.
// Carry the newest such folder over once, so updating does not silently reset settings and
// swap the local CA (which browsers were told to trust) for a new one.
void migrateAppImageConfig(const QString& configDir) {
  if (QFileInfo::exists(configDir + "/session_settings.json")) return;
  const QDir parent = QFileInfo(configDir).dir();
  QFileInfo newest;
  for (const QFileInfo& d : parent.entryInfoList({"droppix*.AppImage"}, QDir::Dirs)) {
    const QFileInfo settings(d.filePath() + "/session_settings.json");
    if (settings.exists() && (!newest.exists() || settings.lastModified() > newest.lastModified()))
      newest = settings;
  }
  if (!newest.exists()) return;
  QDir().mkpath(configDir);
  for (const QFileInfo& f : newest.dir().entryInfoList(QDir::Files)) {
    const QString to = configDir + "/" + f.fileName();
    if (!QFileInfo::exists(to)) QFile::copy(f.filePath(), to);
  }
}
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  // Qt otherwise names the config dir after the executable, so each AppImage filename
  // got a fresh ~/.config/<file>.AppImage and silently lost every saved setting.
  app.setApplicationName("droppix_gui");
  app.setStyle("Fusion");                  // consistent base for the custom dark QSS
  app.setStyleSheet(droppix::styleSheet(droppix::Theme::Dark));
  app.setWindowIcon(QIcon(":/icon.png"));  // taskbar / window-manager icon

  // --minimized: launch-at-login passes this so the GUI doesn't pop a window every
  // boot; starts hidden to the tray (falls back to a normal show() if no tray exists).
  bool minimized = false;
  for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--minimized") minimized = true;

  const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir().mkpath(configDir);
  // Two GUIs scan the same USB devices, auto-connect the same tablets, fight over port
  // 27000, and the second one's cert regeneration invalidates the first one's pairing code.
  QLockFile instanceLock(configDir + "/gui.lock");
  instanceLock.setStaleLockTime(0);   // held for the whole run; a dead owner's lock is reclaimed
  if (!instanceLock.tryLock(200)) {
    if (!minimized)
      QMessageBox::information(nullptr, "Droppix",
          "Droppix is already running (check the system tray).");
    return 0;
  }
  migrateAppImageConfig(configDir);

  droppix::MainWindow w;
  if (minimized) w.startMinimizedToTray(); else w.show();
  return app.exec();
}
