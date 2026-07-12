#pragma once

#include <sys/types.h>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

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
  ProcessManager(RunConfig config, std::string executable_path, std::string module_dir);
  ~ProcessManager();

  ProcessManager(const ProcessManager&) = delete;
  ProcessManager& operator=(const ProcessManager&) = delete;

  bool SwitchMode(const std::string& mode, std::string* error);
  bool StartModule(const std::string& name, std::string* error);
  bool StopModule(const std::string& name, std::string* error);
  bool RestartModule(const std::string& name, std::string* error);
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
  void HandleExit(ProcessRecord* process, int wait_status);
  std::string LibraryPath(const std::string& library) const;

  RunConfig config_;
  std::string executable_path_;
  std::string module_dir_;
  std::string mode_;
  std::vector<ProcessRecord> processes_;
};

}  // namespace navigator
}  // namespace cockpit
