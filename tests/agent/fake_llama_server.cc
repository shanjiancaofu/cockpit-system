#include <signal.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t running = 1;

void HandleSignal(int signal) {
  if (signal == SIGUSR1) {
    _exit(17);
  }
  running = 0;
}

}  // namespace

int main() {
  struct sigaction action {};
  action.sa_handler = HandleSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGUSR1, &action, nullptr);
  while (running != 0) {
    pause();
  }
  return 0;
}
