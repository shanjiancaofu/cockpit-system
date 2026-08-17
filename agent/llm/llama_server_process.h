#pragma once

#include <sys/types.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace voice {

struct LlamaServerProcessConfig {
  std::string executable;
  std::string model_path;
  std::string model_alias = "Qwen3.5-2B";
  std::string host = "127.0.0.1";
  std::uint16_t port = 8080;
  int context_size = 2048;
  int gpu_layers = 0;
  std::chrono::milliseconds startup_timeout{60000};
  std::chrono::milliseconds shutdown_timeout{5000};
  std::chrono::milliseconds restart_delay{1000};
  std::chrono::milliseconds restart_max_delay{30000};
  int restart_limit = 5;
  std::chrono::milliseconds health_check_interval{1000};
  int health_failure_threshold = 3;
};

class LlamaServerProcess {
 public:
  using HealthProbe =
      std::function<bool(const std::string&, std::uint16_t, std::chrono::milliseconds)>;

  explicit LlamaServerProcess(LlamaServerProcessConfig config, HealthProbe health_probe = {});
  ~LlamaServerProcess();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(LlamaServerProcess);

  bool Start(std::string* error);
  void Stop();
  pid_t pid() const;
  bool ready() const;
  std::uint64_t restart_count() const;
  std::string last_error() const;
  bool WaitForRestartCount(std::uint64_t count, std::chrono::milliseconds timeout) const;

 private:
  bool SpawnAndWaitUntilReady(std::string* error);
  bool Spawn(pid_t* pid, std::string* error) const;
  bool WaitUntilReady(pid_t pid, std::string* error);
  void MonitorLoop();
  void TerminateAndReap(pid_t pid) const;
  static bool ProbeHttpHealth(const std::string& host, std::uint16_t port,
                              std::chrono::milliseconds timeout);

  const LlamaServerProcessConfig config_;
  const HealthProbe health_probe_;
  mutable std::mutex mutex_;
  mutable std::condition_variable changed_;
  pid_t pid_ = 0;
  bool ready_ = false;
  bool stopping_ = false;
  std::uint64_t restart_count_ = 0;
  std::string last_error_;
  std::thread monitor_;
};

}  // namespace voice
}  // namespace cockpit
