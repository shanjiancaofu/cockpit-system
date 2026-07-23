#pragma once

#include <sys/types.h>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "cockpit/navigator/diagnostics/crash_reporter.h"
#include "cockpit/navigator/run_config/run_config.h"

namespace cockpit {
namespace navigator {

enum class ProcessState {
  kStopped,
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
};

class ProcessManager {
 public:
  ProcessManager(RunConfig config, std::string executable_path, std::string module_dir,
                 std::string module_config_path, std::string crash_report_directory);
  ~ProcessManager();

  ProcessManager(const ProcessManager&) = delete;
  ProcessManager& operator=(const ProcessManager&) = delete;

  bool SwitchMode(const std::string& mode, std::string* error);
  bool StartModule(const std::string& name, std::string* error);
  bool StopModule(const std::string& name, std::string* error);
  bool RestartModule(const std::string& name, std::string* error);
  bool Reload(std::string* error);
  void ReapExited();
  void StopAll();

  const std::string& mode() const {
    return mode_;
  }
  std::vector<ProcessStatus> Status() const;

 private:
  struct ProcessRecord {
    ModuleConfig config;
    ProcessState state{ProcessState::kStopped};
    pid_t pid{0};
    int last_exit_code{0};
    bool desired{false};
    std::deque<std::chrono::steady_clock::time_point> restart_times;
  };

  ProcessRecord* Find(const std::string& name);
  bool Start(ProcessRecord* process, std::string* error);
  void HandleExit(ProcessRecord* process, pid_t exited_pid, int wait_status);
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
};

}  // namespace navigator
}  // namespace cockpit
