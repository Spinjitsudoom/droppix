#pragma once
#include <QObject>
#include <QProcess>

class QSocketNotifier;

namespace droppix {

// Long-running helpers (avahi-publish-service, avahi-browse) must not outlive the GUI.
// Qt only stops them from destructors, which never run when the GUI is killed (logout,
// `kill`, a crash), so each killed GUI left its mDNS announcement running forever. This
// asks the kernel to SIGTERM the child when the GUI dies. Not for pkexec children: the
// kernel clears the setting when a setuid program starts.
void endWithParent(QProcess& p);

// Turns SIGTERM / SIGINT / SIGHUP into a Qt signal, so the GUI can shut down the same way
// the tray's Quit does (stop streamers, release the evdi screen) instead of dying
// mid-session. One instance at a time; the destructor restores the default handlers.
class QuitSignals : public QObject {
  Q_OBJECT
 public:
  explicit QuitSignals(QObject* parent = nullptr);
  ~QuitSignals() override;
 signals:
  void requested(int signo);
 private:
  int fds_[2] = {-1, -1};
  QSocketNotifier* notifier_ = nullptr;
};

}  // namespace droppix
