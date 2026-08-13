#pragma once

#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cockpit/navigator/diagnostics/crash_reporter.h"
#include "cockpit/navigator/run_config/run_config.h"

namespace cockpit {
namespace navigator {

enum class ProcessState {
  kStopped,
  kRestarting,
  kRunning,
  kFailed,
};

const char* ToString(ProcessState state);

struct ProcessStatus {
  std::string name;
  ProcessState state{ProcessState::kStopped};
  pid_t pid{0};
  int last_exit_code{0};
  int restart_count{0};
  std::uint64_t start_count{0};
  std::int64_t uptime_ms{0};
  std::int64_t last_failure_ms{0};
  int last_signal{0};
};

class ProcessManager {
 public:
  using StopFailureInjector = std::function<bool(ModuleId)>;

  ProcessManager(RunConfig config, std::string executable_path, std::string module_dir,
                 std::string module_config_path, std::string crash_report_directory,
                 StopFailureInjector stop_failure_injector = {});
  ~ProcessManager();

  ProcessManager(const ProcessManager&) = delete;
  ProcessManager& operator=(const ProcessManager&) = delete;

  bool SwitchMode(const std::string& mode, std::string* error);
  bool SwitchMode(RunMode mode, std::string* error);
  bool StartModule(const std::string& name, std::string* error);
  bool StopModule(const std::string& name, std::string* error);
  bool RestartModule(const std::string& name, std::string* error);
  bool Reload(std::string* error);
  void ReapExited();
  void StopAll();
  bool HasCriticalFailure() const {
    return critical_failure_;
  }

  const std::string& mode() const {
    return mode_;
  }
  std::vector<ProcessStatus> Status() const;

 private:
  struct ProcessRecord {
    explicit ProcessRecord(ModuleConfig module_config) : config(std::move(module_config)) {
    }

    ModuleConfig config;
    ProcessState state{ProcessState::kStopped};
    pid_t pid{0};
    int last_exit_code{0};
    bool desired{false};
    std::deque<std::chrono::steady_clock::time_point> restart_times;
    std::uint64_t start_count{0};
    std::chrono::steady_clock::time_point started_at;
    std::int64_t last_failure_ms{0};
    int last_signal{0};
    bool restart_pending{false};
    std::chrono::steady_clock::time_point restart_at;
    pid_t failed_pid{0};
    int failed_wait_status{0};
  };

  ProcessRecord* Find(ModuleId module);
  bool StartModule(ModuleId module, std::string* error);
  bool StopModule(ModuleId module, std::string* error);
  bool Start(ProcessRecord* process, std::string* error);
  void HandleExit(ProcessRecord* process, pid_t exited_pid, int wait_status);
  void RestartPending();
  void MarkFailed(ProcessRecord* process);
  void RecordCrash(const ProcessRecord& process, pid_t exited_pid, int wait_status,
                   const std::string& restart_result);
  std::string LibraryPath(const std::string& library) const;

  RunConfig config_;
  std::string executable_path_;
  std::string module_dir_;
  std::string module_config_path_;
  CrashReporter crash_reporter_;
  std::string mode_;
  std::vector<ProcessRecord> processes_;
  bool critical_failure_{false};
  StopFailureInjector stop_failure_injector_;
};

}  // namespace navigator
}  // namespace cockpit
