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

}  // namespace

const char* ToString(ProcessState state) {
  switch (state) {
    case ProcessState::kStopped:
      return "stopped";
    case ProcessState::kRunning:
      return "running";
    case ProcessState::kFailed:
      return "failed";
  }
  return "unknown";
}

ProcessManager::ProcessManager(RunConfig config, std::string executable_path,
                               std::string module_dir, std::string module_config_path,
                               std::string crash_report_directory)
    : config_(std::move(config)),
      executable_path_(std::move(executable_path)),
      module_dir_(std::move(module_dir)),
      module_config_path_(std::move(module_config_path)),
      crash_reporter_(std::move(crash_report_directory)) {
  for (const ModuleConfig& module : config_.modules) {
    processes_.push_back(ProcessRecord{module});
  }
}

ProcessManager::~ProcessManager() {
  StopAll();
}

bool ProcessManager::SwitchMode(const std::string& mode, std::string* error) {
  const auto mode_it = config_.modes.find(mode);
  if (mode_it == config_.modes.end()) {
    *error = "unknown mode: " + mode;
    return false;
  }
  const std::unordered_set<std::string> desired(mode_it->second.begin(), mode_it->second.end());
  std::unordered_set<std::string> previous;
  std::vector<std::string> previous_order;
  for (const ProcessRecord& process : processes_) {
    if (process.desired) {
      previous.insert(process.config.name);
      previous_order.push_back(process.config.name);
    }
  }

  for (auto process = processes_.rbegin(); process != processes_.rend(); ++process) {
    if (process->desired && desired.find(process->config.name) == desired.end()) {
      std::string stop_error;
      if (!StopModule(process->config.name, &stop_error)) {
        *error = stop_error;
        return false;
      }
    }
  }
  for (const std::string& name : mode_it->second) {
    ProcessRecord* process = Find(name);
    if (!process->desired || process->state != ProcessState::kRunning) {
      if (!StartModule(name, error)) {
        const std::string switch_error = *error;
        bool rollback_ok = true;
        for (auto current = processes_.rbegin(); current != processes_.rend(); ++current) {
          if (current->desired && previous.find(current->config.name) == previous.end()) {
            std::string rollback_error;
            rollback_ok &= StopModule(current->config.name, &rollback_error);
          }
        }
        for (const std::string& previous_name : previous_order) {
          ProcessRecord* previous_process = Find(previous_name);
          if (previous_process->state != ProcessState::kRunning) {
            std::string rollback_error;
            rollback_ok &= StartModule(previous_name, &rollback_error);
          }
        }
        *error = switch_error + (rollback_ok ? "" : "; rollback failed");
        return false;
      }
    }
  }
  mode_ = mode;
  return true;
}

bool ProcessManager::StartModule(const std::string& name, std::string* error) {
  ProcessRecord* process = Find(name);
  if (process == nullptr) {
    *error = "unknown module: " + name;
    return false;
  }
  if (process->state == ProcessState::kRunning) {
    process->desired = true;
    return true;
  }
  process->desired = true;
  process->restart_times.clear();
  return Start(process, error);
}

bool ProcessManager::StopModule(const std::string& name, std::string* error) {
  ProcessRecord* process = Find(name);
  if (process == nullptr) {
    *error = "unknown module: " + name;
    return false;
  }
  process->desired = false;
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
      return;
    }
    for (ProcessRecord& process : processes_) {
      if (process.pid == pid) {
        HandleExit(&process, pid, wait_status);
        break;
      }
    }
  }
}

void ProcessManager::StopAll() {
  for (auto process = processes_.rbegin(); process != processes_.rend(); ++process) {
    if (process->pid != 0) {
      std::string error;
      StopModule(process->config.name, &error);
    }
  }
  mode_.clear();
}

std::vector<ProcessStatus> ProcessManager::Status() const {
  std::vector<ProcessStatus> status;
  status.reserve(processes_.size());
  for (const ProcessRecord& process : processes_) {
    status.push_back({process.config.name, process.state, process.pid, process.last_exit_code,
                      static_cast<int>(process.restart_times.size())});
  }
  return status;
}

ProcessManager::ProcessRecord* ProcessManager::Find(const std::string& name) {
  for (ProcessRecord& process : processes_) {
    if (process.config.name == name) {
      return &process;
    }
  }
  return nullptr;
}

bool ProcessManager::Start(ProcessRecord* process, std::string* error) {
  const std::string library_path = LibraryPath(process->config.library);
  int ready_pipe[2];
  if (pipe2(ready_pipe, O_CLOEXEC) < 0) {
    *error = "failed to create readiness pipe for " + process->config.name;
    process->state = ProcessState::kFailed;
    return false;
  }
  const std::string ready_fd = std::to_string(ready_pipe[1]);
  std::vector<std::string> arguments{
      executable_path_, "--module-child",  "--module",          process->config.name, "--library",
      library_path,     "--module-config", module_config_path_, "--ready-fd",         ready_fd,
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
    *error = "failed to spawn module " + process->config.name + ": " +
             std::string(std::strerror(spawn_result));
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
    *error = "module " + process->config.name + " failed before ready";
    return false;
  }

  process->pid = pid;
  process->state = ProcessState::kRunning;
  process->last_exit_code = 0;
  LOG_INFO("started module " + process->config.name + " pid=" + std::to_string(pid));
  return true;
}

void ProcessManager::HandleExit(ProcessRecord* process, pid_t exited_pid, int wait_status) {
  process->pid = 0;
  process->last_exit_code = ExitCode(wait_status);
  if (!process->desired) {
    process->state = ProcessState::kStopped;
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto window = std::chrono::seconds(config_.restart_window_seconds);
  while (!process->restart_times.empty() && now - process->restart_times.front() > window) {
    process->restart_times.pop_front();
  }
  if (static_cast<int>(process->restart_times.size()) >= config_.max_restarts) {
    process->state = ProcessState::kFailed;
    RecordCrash(*process, exited_pid, wait_status, "limit_exceeded");
    LOG_ERROR("module " + process->config.name + " exceeded restart limit");
    return;
  }

  process->restart_times.push_back(now);
  std::string error;
  if (!Start(process, &error)) {
    process->state = ProcessState::kFailed;
    RecordCrash(*process, exited_pid, wait_status, "failed");
    LOG_ERROR(error);
    return;
  }
  RecordCrash(*process, exited_pid, wait_status, "succeeded");
}

void ProcessManager::RecordCrash(const ProcessRecord& process, pid_t exited_pid, int wait_status,
                                 const std::string& restart_result) {
  CrashReport report;
  report.timestamp_ms = time::NowMs();
  report.module = process.config.name;
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
