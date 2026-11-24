#include <gtest/gtest.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <csignal>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include "process_lifetime.h"

using namespace droppix;

namespace {
void ensureQApplication() {
  if (QApplication::instance()) return;
  static int argc = 1;
  static char arg0[] = "droppix_gui_tests";
  static char* argv[] = {arg0, nullptr};
  static QApplication app(argc, argv);
}

// Running = /proc entry exists and is not a zombie.
bool running(pid_t pid) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
  std::string stat;
  if (!std::getline(f, stat)) return false;
  const auto close = stat.rfind(')');
  return close != std::string::npos && close + 2 < stat.size() && stat[close + 2] != 'Z';
}
}  // namespace

// A GUI that is killed runs no destructors. Its helper must still end.
TEST(ProcessLifetime, HelperEndsWhenItsParentDies) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  const pid_t gui = fork();
  ASSERT_GE(gui, 0);
  if (gui == 0) {
    close(fds[0]);
    QProcess p;
    endWithParent(p);
    p.start("sleep", {"30"});
    const pid_t helper = p.waitForStarted(3000) ? static_cast<pid_t>(p.processId()) : -1;
    (void)!write(fds[1], &helper, sizeof helper);
    _exit(0);   // no destructors, the way a killed GUI dies
  }
  close(fds[1]);
  pid_t helper = -1;
  ASSERT_EQ(read(fds[0], &helper, sizeof helper), static_cast<ssize_t>(sizeof helper));
  close(fds[0]);
  waitpid(gui, nullptr, 0);
  ASSERT_GT(helper, 0);

  bool ended = false;
  for (int i = 0; i < 100 && !ended; ++i) { ended = !running(helper); if (!ended) usleep(20000); }
  if (!ended) kill(helper, SIGKILL);
  EXPECT_TRUE(ended) << "helper outlived its parent";
}

TEST(ProcessLifetime, TermHupAndIntRequestAQuit) {
  ensureQApplication();
  QuitSignals quit;
  for (int s : {SIGTERM, SIGHUP, SIGINT}) {
    struct sigaction sa {};
    sigaction(s, nullptr, &sa);
    // Raising an unhandled one would kill the test binary, so check first.
    ASSERT_NE(sa.sa_handler, SIG_DFL) << "signal " << s << " not handled";
    ASSERT_NE(sa.sa_handler, SIG_IGN) << "signal " << s << " ignored";
  }
  std::vector<int> got;
  QObject::connect(&quit, &QuitSignals::requested, [&](int s) { got.push_back(s); });
  raise(SIGTERM);
  raise(SIGHUP);
  QElapsedTimer t; t.start();
  while (got.size() < 2 && t.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  EXPECT_EQ(got, (std::vector<int>{SIGTERM, SIGHUP}));
}

TEST(ProcessLifetime, DestroyingRestoresDefaultHandlers) {
  ensureQApplication();
  { QuitSignals quit; }
  struct sigaction sa {};
  sigaction(SIGTERM, nullptr, &sa);
  EXPECT_EQ(sa.sa_handler, SIG_DFL);
}
