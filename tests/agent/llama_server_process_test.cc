#include "agent/llm/llama_server_process.h"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: llama_server_process_test FAKE_SERVER\n";
    return 2;
  }

  cockpit::voice::LlamaServerProcessConfig config;
  config.executable = argv[1];
  config.model_path =
      "/tmp/cockpit-fake-llama-model-" + std::to_string(static_cast<long long>(getpid())) + ".gguf";
  std::ofstream(config.model_path) << "GGUF";
  config.startup_timeout = std::chrono::seconds(1);
  config.shutdown_timeout = std::chrono::seconds(1);
  config.restart_delay = std::chrono::milliseconds(10);
  config.health_check_interval = std::chrono::milliseconds(10);
  config.health_failure_threshold = 1;
  std::atomic_bool fail_health{false};
  cockpit::voice::LlamaServerProcess process(
      config, [&fail_health](const std::string&, std::uint16_t, std::chrono::milliseconds) {
        return !fail_health.exchange(false);
      });

  std::string error;
  if (!Check(process.Start(&error), "managed llama-server failed to start") ||
      !Check(process.ready(), "managed llama-server was not ready")) {
    std::cerr << error << '\n';
    std::filesystem::remove(config.model_path);
    return 1;
  }
  const pid_t initial_pid = process.pid();
  if (!Check(initial_pid > 0, "managed llama-server PID was invalid") ||
      !Check(kill(initial_pid, SIGUSR1) == 0, "failed to crash fake llama-server") ||
      !Check(process.WaitForRestartCount(1, std::chrono::seconds(3)),
             "managed llama-server did not restart") ||
      !Check(process.pid() > 0 && process.pid() != initial_pid,
             "managed llama-server replacement PID was invalid")) {
    process.Stop();
    std::filesystem::remove(config.model_path);
    return 1;
  }

  const pid_t replacement_pid = process.pid();
  fail_health.store(true);
  if (!Check(process.WaitForRestartCount(2, std::chrono::seconds(3)),
             "unhealthy llama-server did not restart") ||
      !Check(process.pid() > 0 && process.pid() != replacement_pid,
             "health replacement PID was invalid")) {
    process.Stop();
    std::filesystem::remove(config.model_path);
    return 1;
  }

  const pid_t health_replacement_pid = process.pid();
  process.Stop();
  std::filesystem::remove(config.model_path);
  errno = 0;
  if (!Check(!process.ready() && process.pid() == 0, "managed llama-server remained active") ||
      !Check(kill(health_replacement_pid, 0) < 0 && errno == ESRCH,
             "managed llama-server child was not reaped")) {
    return 1;
  }

  std::cout << "llama-server process lifecycle tests passed\n";
  return 0;
}
