#include "cockpit/navigator/process/process_manager.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

extern char** environ;

namespace cockpit {
namespace navigator {
namespace {

int ExitCode(int wait_status) {
  if (WIFEXITED(wait_status)) {
    return WEXITSTATUS(wait_status);
  }
  if (WIFSIGNALED(wait_status)) {
    return 128 + WTERMSIG(wait_status);
  }
  return -1;
}

int TerminationSignal(int wait_status) {
  return WIFSIGNALED(wait_status) ? WTERMSIG(wait_status) : 0;
}

bool CoreDumped(int wait_status) {
  return WIFSIGNALED(wait_status) && WCOREDUMP(wait_status);
}

const char* TerminationKind(int wait_status) {
  if (WIFEXITED(wait_status)) {
    return "exit";
  }
  if (WIFSIGNALED(wait_status)) {
    return "signal";
  }
  return "unknown";
}

void TerminateRemainingProcessGroup(pid_t leader_pid) {
  if (leader_pid <= 0) {
    return;
  }
  if (kill(-leader_pid, SIGKILL) < 0 && errno != ESRCH) {
    LOG_ERROR("failed to terminate descendants of module pid=" + std::to_string(leader_pid) + ": " +
              std::strerror(errno));
  }
}

}  // namespace

const char* ToString(ProcessState state) {
  switch (state) {
    case ProcessState::kStopped:
      return "stopped";
    case ProcessState::kRestarting:
      return "restarting";
    case ProcessState::kRunning:
      return "running";
    case ProcessState::kFailed:
      return "failed";
  }
  return "unknown";
}

ProcessManager::ProcessManager(RunConfig config, std::string executable_path,
                               std::string module_dir, std::string module_config_path,
                               std::string crash_report_directory,
                               StopFailureInjector stop_failure_injector)
    : config_(std::move(config)),
      executable_path_(std::move(executable_path)),
      module_dir_(std::move(module_dir)),
      module_config_path_(std::move(module_config_path)),
      crash_reporter_(std::move(crash_report_directory)),
      stop_failure_injector_(std::move(stop_failure_injector)) {
  for (const ModuleConfig& module : config_.modules) {
    processes_.emplace_back(module);
  }
}

ProcessManager::~ProcessManager() {
  StopAll();
}

bool ProcessManager::SwitchMode(const std::string& mode, std::string* error) {
  RunMode parsed_mode;
  if (!ParseRunMode(mode, &parsed_mode)) {
    *error = "unknown mode: " + mode;
    return false;
  }
  return SwitchMode(parsed_mode, error);
}

bool ProcessManager::SwitchMode(RunMode mode, std::string* error) {
  const std::vector<ModuleId>* mode_modules = config_.FindMode(mode);
  if (mode_modules == nullptr) {
    *error = "unknown mode: " + std::string(RunModeName(mode));
    return false;
  }
  const std::unordered_set<ModuleId> desired(mode_modules->begin(), mode_modules->end());
  std::unordered_set<ModuleId> previous;
  std::vector<ModuleId> previous_order;
  for (const ProcessRecord& process : processes_) {
    if (process.desired) {
      previous.insert(process.config.id);
      previous_order.push_back(process.config.id);
    }
  }

  const auto restore_previous_mode = [this, &previous, &previous_order]() {
    bool rollback_ok = true;
    for (auto current = processes_.rbegin(); current != processes_.rend(); ++current) {
      if (current->desired && previous.find(current->config.id) == previous.end()) {
        std::string rollback_error;
        rollback_ok &= StopModule(current->config.id, &rollback_error);
      }
    }
    for (ModuleId previous_module : previous_order) {
      std::string rollback_error;
      rollback_ok &= StartModule(previous_module, &rollback_error);
    }
    return rollback_ok;
  };

  for (auto process = processes_.rbegin(); process != processes_.rend(); ++process) {
    if (process->desired && desired.find(process->config.id) == desired.end()) {
      std::string stop_error;
      if (!StopModule(process->config.id, &stop_error)) {
        const bool rollback_ok = restore_previous_mode();
        *error = stop_error + (rollback_ok ? "" : "; rollback failed");
        return false;
      }
    }
  }
  for (ModuleId module : *mode_modules) {
    ProcessRecord* process = Find(module);
    if (!process->desired || process->state != ProcessState::kRunning) {
      if (!StartModule(module, error)) {
        const std::string switch_error = *error;
        const bool rollback_ok = restore_previous_mode();
        *error = switch_error + (rollback_ok ? "" : "; rollback failed");
        return false;
      }
    }
  }
  mode_ = RunModeName(mode);
  critical_failure_ = false;
  return true;
}

bool ProcessManager::StartModule(const std::string& name, std::string* error) {
  ModuleId module;
  if (!ParseModuleId(name, &module)) {
    *error = "unknown module: " + name;
    return false;
  }
  return StartModule(module, error);
}

bool ProcessManager::StartModule(ModuleId module, std::string* error) {
  ProcessRecord* process = Find(module);
  if (process == nullptr) {
    *error = "module is not configured: " + std::string(ModuleName(module));
    return false;
  }
  if (process->state == ProcessState::kRunning) {
    process->desired = true;
    return true;
  }
  process->desired = true;
  process->restart_pending = false;
  process->restart_times.clear();
  return Start(process, error);
}

bool ProcessManager::StopModule(const std::string& name, std::string* error) {
  ModuleId module;
  if (!ParseModuleId(name, &module)) {
    *error = "unknown module: " + name;
    return false;
  }
  return StopModule(module, error);
}

bool ProcessManager::StopModule(ModuleId module, std::string* error) {
  ProcessRecord* process = Find(module);
  const std::string name = ModuleName(module);
  if (process == nullptr) {
    *error = "module is not configured: " + name;
    return false;
  }
  if (stop_failure_injector_ && stop_failure_injector_(module)) {
    *error = "injected stop failure for module " + name;
    return false;
  }
  process->desired = false;
  process->restart_pending = false;
  process->restart_times.clear();
  if (process->pid == 0) {
    process->state = ProcessState::kStopped;
    return true;
  }

  const pid_t target_pid = process->pid;
  if (kill(-target_pid, SIGTERM) < 0 && errno != ESRCH) {
    *error = "failed to terminate module " + name + ": " + std::strerror(errno);
    return false;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.stop_timeout_ms);
  int wait_status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(target_pid, &wait_status, WNOHANG);
    if (result == target_pid) {
      process->pid = 0;
      process->state = ProcessState::kStopped;
      process->last_exit_code = ExitCode(wait_status);
      return true;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0 && errno == ECHILD && kill(target_pid, 0) < 0 && errno == ESRCH) {
      process->pid = 0;
      process->state = ProcessState::kStopped;
      process->last_exit_code = 0;
      return true;
    }
    if (result < 0) {
      *error = "failed while waiting for module " + name + ": " + std::strerror(errno);
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (kill(-target_pid, SIGKILL) < 0 && errno != ESRCH) {
    *error = "failed to kill module " + name + ": " + std::strerror(errno);
    return false;
  }
  pid_t wait_result = 0;
  do {
    wait_result = waitpid(target_pid, &wait_status, 0);
  } while (wait_result < 0 && errno == EINTR);
  if (wait_result < 0 && !(errno == ECHILD && kill(target_pid, 0) < 0 && errno == ESRCH)) {
    *error = "module " + name + " termination state is unknown: " + std::strerror(errno);
    return false;
  }
  process->pid = 0;
  process->state = ProcessState::kStopped;
  process->last_exit_code = wait_result == target_pid ? ExitCode(wait_status) : 0;
  return true;
}

bool ProcessManager::RestartModule(const std::string& name, std::string* error) {
  if (!StopModule(name, error)) {
    return false;
  }
  return StartModule(name, error);
}

bool ProcessManager::Reload(std::string* error) {
  const std::string mode = mode_;
  if (mode.empty()) {
    *error = "runtime mode is not active";
    return false;
  }
  StopAll();
  return SwitchMode(mode, error);
}

void ProcessManager::ReapExited() {
  int wait_status = 0;
  while (true) {
    const pid_t pid = waitpid(-1, &wait_status, WNOHANG);
    if (pid <= 0) {
      break;
    }
    for (ProcessRecord& process : processes_) {
      if (process.pid == pid) {
        HandleExit(&process, pid, wait_status);
        break;
      }
    }
  }
  RestartPending();
}

void ProcessManager::StopAll() {
  for (auto process = processes_.rbegin(); process != processes_.rend(); ++process) {
    if (process->pid != 0 || process->desired || process->restart_pending) {
      std::string error;
      StopModule(process->config.id, &error);
    }
  }
  mode_.clear();
  critical_failure_ = false;
}

std::vector<ProcessStatus> ProcessManager::Status() const {
  const auto now = std::chrono::steady_clock::now();
  std::vector<ProcessStatus> status;
  status.reserve(processes_.size());
  for (const ProcessRecord& process : processes_) {
    const std::int64_t uptime_ms =
        process.state == ProcessState::kRunning
            ? std::chrono::duration_cast<std::chrono::milliseconds>(now - process.started_at)
                  .count()
            : 0;
    status.push_back({ModuleName(process.config.id), process.state, process.pid,
                      process.last_exit_code, static_cast<int>(process.restart_times.size()),
                      process.start_count, uptime_ms, process.last_failure_ms,
                      process.last_signal});
  }
  return status;
}

ProcessManager::ProcessRecord* ProcessManager::Find(ModuleId module) {
  for (ProcessRecord& process : processes_) {
    if (process.config.id == module) {
      return &process;
    }
  }
  return nullptr;
}

bool ProcessManager::Start(ProcessRecord* process, std::string* error) {
  const std::string module_name = ModuleName(process->config.id);
  const std::string library_path = LibraryPath(process->config.library);
  int ready_pipe[2];
  if (pipe2(ready_pipe, O_CLOEXEC) < 0) {
    *error = "failed to create readiness pipe for " + module_name;
    process->state = ProcessState::kFailed;
    return false;
  }
  const std::string ready_fd = std::to_string(ready_pipe[1]);
  std::vector<std::string> arguments{
      executable_path_, "--module-child",  "--module",          module_name,  "--library",
      library_path,     "--module-config", module_config_path_, "--ready-fd", ready_fd,
  };
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1U);
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  const int actions_result = posix_spawn_file_actions_init(&actions);
  const int attributes_result = actions_result == 0 ? posix_spawnattr_init(&attributes) : EINVAL;
  bool spawn_configuration_ok = actions_result == 0 && attributes_result == 0;
  if (spawn_configuration_ok) {
    spawn_configuration_ok =
        posix_spawn_file_actions_addclose(&actions, ready_pipe[0]) == 0 &&
        posix_spawn_file_actions_adddup2(&actions, ready_pipe[1], ready_pipe[1]) == 0 &&
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0 &&
        posix_spawnattr_setpgroup(&attributes, 0) == 0;
  }
  pid_t pid = 0;
  const int spawn_result =
      spawn_configuration_ok
          ? posix_spawn(&pid, executable_path_.c_str(), &actions, &attributes, argv.data(), environ)
          : EINVAL;
  if (attributes_result == 0) {
    posix_spawnattr_destroy(&attributes);
  }
  if (actions_result == 0) {
    posix_spawn_file_actions_destroy(&actions);
  }
  if (spawn_result != 0) {
    close(ready_pipe[0]);
    close(ready_pipe[1]);
    *error =
        "failed to spawn module " + module_name + ": " + std::string(std::strerror(spawn_result));
    process->state = ProcessState::kFailed;
    return false;
  }

  close(ready_pipe[1]);
  pollfd descriptor{ready_pipe[0], POLLIN | POLLHUP, 0};
  const int poll_result = poll(&descriptor, 1, config_.startup_timeout_ms);
  char ready = 0;
  const ssize_t read_size = poll_result > 0 ? read(ready_pipe[0], &ready, 1) : -1;
  close(ready_pipe[0]);
  if (read_size != 1 || ready != '1') {
    kill(-pid, SIGKILL);
    int wait_status = 0;
    while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
    }
    process->pid = 0;
    process->state = ProcessState::kFailed;
    process->last_exit_code = ExitCode(wait_status);
    *error = "module " + module_name + " failed before ready";
    return false;
  }

  process->pid = pid;
  process->state = ProcessState::kRunning;
  process->last_exit_code = 0;
  process->started_at = std::chrono::steady_clock::now();
  ++process->start_count;
  LOG_INFO("started module " + module_name + " pid=" + std::to_string(pid));
  return true;
}

void ProcessManager::HandleExit(ProcessRecord* process, pid_t exited_pid, int wait_status) {
  TerminateRemainingProcessGroup(exited_pid);
  process->pid = 0;
  process->last_exit_code = ExitCode(wait_status);
  process->last_failure_ms = time::WallTime::Now().ToMilliseconds();
  process->last_signal = TerminationSignal(wait_status);
  if (!process->desired) {
    process->state = ProcessState::kStopped;
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto window = std::chrono::seconds(config_.restart_window_seconds);
  while (!process->restart_times.empty() && now - process->restart_times.front() > window) {
    process->restart_times.pop_front();
  }
  if (static_cast<int>(process->restart_times.size()) >= process->config.restart_limit) {
    MarkFailed(process);
    RecordCrash(*process, exited_pid, wait_status, "limit_exceeded");
    LOG_ERROR("module " + std::string(ModuleName(process->config.id)) + " exceeded restart limit");
    return;
  }

  process->restart_times.push_back(now);
  process->state = ProcessState::kRestarting;
  process->restart_pending = true;
  process->restart_at = now + std::chrono::milliseconds(process->config.restart_delay_ms);
  process->failed_pid = exited_pid;
  process->failed_wait_status = wait_status;
}

void ProcessManager::RestartPending() {
  const auto now = std::chrono::steady_clock::now();
  for (ProcessRecord& process : processes_) {
    if (!process.restart_pending || now < process.restart_at) {
      continue;
    }
    process.restart_pending = false;
    const pid_t failed_pid = process.failed_pid;
    const int failed_wait_status = process.failed_wait_status;
    process.failed_pid = 0;
    process.failed_wait_status = 0;

    std::string error;
    if (!Start(&process, &error)) {
      MarkFailed(&process);
      RecordCrash(process, failed_pid, failed_wait_status, "failed");
      LOG_ERROR(error);
      continue;
    }
    RecordCrash(process, failed_pid, failed_wait_status, "succeeded");
  }
}

void ProcessManager::MarkFailed(ProcessRecord* process) {
  process->state = ProcessState::kFailed;
  process->restart_pending = false;
  if (process->config.critical) {
    critical_failure_ = true;
  }
}

void ProcessManager::RecordCrash(const ProcessRecord& process, pid_t exited_pid, int wait_status,
                                 const std::string& restart_result) {
  CrashReport report;
  report.timestamp_ms = time::WallTime::Now().ToMilliseconds();
  report.module = ModuleName(process.config.id);
  report.mode = mode_;
  report.pid = exited_pid;
  report.termination = TerminationKind(wait_status);
  report.exit_code = ExitCode(wait_status);
  report.signal = TerminationSignal(wait_status);
  report.core_dumped = CoreDumped(wait_status);
  report.restart_result = restart_result;
  report.restart_count = static_cast<int>(process.restart_times.size());
  report.replacement_pid = restart_result == "succeeded" ? process.pid : 0;

  std::string error;
  if (!crash_reporter_.Record(report, &error)) {
    LOG_ERROR(error);
  }
}

std::string ProcessManager::LibraryPath(const std::string& library) const {
  const std::filesystem::path path(library);
  if (path.is_absolute()) {
    return path.string();
  }
  return (std::filesystem::path(module_dir_) / path).string();
}

}  // namespace navigator
}  // namespace cockpit
