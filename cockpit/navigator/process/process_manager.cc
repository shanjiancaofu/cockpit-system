#include "cockpit/navigator/process/process_manager.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

#include "cockpit/core/logging/logger.h"

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
                               std::string module_dir)
    : config_(std::move(config)),
      executable_path_(std::move(executable_path)),
      module_dir_(std::move(module_dir)) {
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

  kill(process->pid, SIGTERM);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.stop_timeout_ms);
  int wait_status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(process->pid, &wait_status, WNOHANG);
    if (result == process->pid || (result < 0 && errno == ECHILD)) {
      const bool reaped = result == process->pid;
      process->pid = 0;
      process->state = ProcessState::kStopped;
      process->last_exit_code = reaped ? ExitCode(wait_status) : 0;
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  kill(process->pid, SIGKILL);
  while (waitpid(process->pid, &wait_status, 0) < 0 && errno == EINTR) {
  }
  process->pid = 0;
  process->state = ProcessState::kStopped;
  process->last_exit_code = ExitCode(wait_status);
  return true;
}

bool ProcessManager::RestartModule(const std::string& name, std::string* error) {
  if (!StopModule(name, error)) {
    return false;
  }
  return StartModule(name, error);
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
        HandleExit(&process, wait_status);
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
  const pid_t pid = fork();
  if (pid < 0) {
    close(ready_pipe[0]);
    close(ready_pipe[1]);
    *error = "failed to fork module " + process->config.name;
    process->state = ProcessState::kFailed;
    return false;
  }
  if (pid == 0) {
    close(ready_pipe[0]);
    fcntl(ready_pipe[1], F_SETFD, 0);
    const std::string ready_fd = std::to_string(ready_pipe[1]);
    execl(executable_path_.c_str(), executable_path_.c_str(), "--module-child", "--module",
          process->config.name.c_str(), "--library", library_path.c_str(), "--module-config",
          process->config.config_path.c_str(), "--ready-fd", ready_fd.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }

  close(ready_pipe[1]);
  pollfd descriptor{ready_pipe[0], POLLIN | POLLHUP, 0};
  const int poll_result = poll(&descriptor, 1, config_.startup_timeout_ms);
  char ready = 0;
  const ssize_t read_size = poll_result > 0 ? read(ready_pipe[0], &ready, 1) : -1;
  close(ready_pipe[0]);
  if (read_size != 1 || ready != '1') {
    kill(pid, SIGKILL);
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

void ProcessManager::HandleExit(ProcessRecord* process, int wait_status) {
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
    LOG_ERROR("module " + process->config.name + " exceeded restart limit");
    return;
  }

  process->restart_times.push_back(now);
  std::string error;
  if (!Start(process, &error)) {
    process->state = ProcessState::kFailed;
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
