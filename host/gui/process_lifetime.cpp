#include "process_lifetime.h"
#include <QSocketNotifier>
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace droppix {

void endWithParent(QProcess& p) {
  const pid_t parent = getpid();
  p.setChildProcessModifier([parent] {
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    // The parent may already have died between fork and prctl; nothing would signal us.
    if (getppid() != parent) _exit(1);
  });
}

namespace {
constexpr int kQuitSignals[] = {SIGTERM, SIGINT, SIGHUP};
int g_write_fd = -1;

void onQuitSignal(int signo) {
  const int saved = errno;
  const unsigned char b = static_cast<unsigned char>(signo);
  if (g_write_fd >= 0) (void)!write(g_write_fd, &b, 1);
  errno = saved;
}
}  // namespace

QuitSignals::QuitSignals(QObject* parent) : QObject(parent) {
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fds_) != 0) {
    fds_[0] = fds_[1] = -1;
    return;
  }
  g_write_fd = fds_[0];
  notifier_ = new QSocketNotifier(fds_[1], QSocketNotifier::Read, this);
  connect(notifier_, &QSocketNotifier::activated, this, [this] {
    unsigned char b;
    while (read(fds_[1], &b, 1) == 1) emit requested(b);
  });
  struct sigaction sa {};
  sa.sa_handler = onQuitSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  for (int s : kQuitSignals) sigaction(s, &sa, nullptr);
}

QuitSignals::~QuitSignals() {
  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  for (int s : kQuitSignals) sigaction(s, &sa, nullptr);
  if (g_write_fd == fds_[0]) g_write_fd = -1;
  delete notifier_;
  notifier_ = nullptr;
  for (int& fd : fds_) if (fd >= 0) { close(fd); fd = -1; }
}

}  // namespace droppix
