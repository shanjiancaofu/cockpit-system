#include "cockpit/navigator/navigator.h"

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "cockpit/core/build/build_info.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/navigator/dl_api/module_loader.h"

namespace cockpit {
namespace navigator {
namespace {

volatile sig_atomic_t signal_received = 0;

void HandleSignal(int) {
  signal_received = 1;
}

void InstallSignalHandlers() {
  struct sigaction action {};
  action.sa_handler = HandleSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
}

}  // namespace

Navigator::Navigator(RunConfig config, std::string executable_path, std::string module_dir,
                     std::string module_config_path, std::string crash_report_directory)
    : config_(std::move(config)),
      process_manager_(config_, std::move(executable_path), std::move(module_dir),
                       std::move(module_config_path), std::move(crash_report_directory)) {
}

int Navigator::Run() {
  InstallSignalHandlers();
  std::string error;
  if (!config_.Validate(&error)) {
    LOG_ERROR(error);
    return 1;
  }
  if (!ipc_.Open(config_.socket_path, &error)) {
    LOG_ERROR(error);
    return 1;
  }
  if (!process_manager_.SwitchMode(config_.initial_mode, &error)) {
    LOG_ERROR(error);
    return 1;
  }
  LOG_INFO("navigator started in mode " + std::string(RunModeName(config_.initial_mode)));

  bool critical_failure = false;
  while (!stop_requested_ && signal_received == 0) {
    std::string request;
    const int client_fd = ipc_.WaitForRequest(100, &request);
    if (client_fd >= 0) {
      ipc_.ReplyAndClose(client_fd, ExecuteCommand(request));
    }
    process_manager_.ReapExited();
    if (process_manager_.HasCriticalFailure()) {
      LOG_ERROR("critical Navigator module failed");
      critical_failure = true;
      break;
    }
  }

  ipc_.Close();
  process_manager_.StopAll();
  LOG_INFO("navigator stopped");
  if (critical_failure) {
    return 1;
  }
  return reexec_mode_.empty() ? 0 : 75;
}

std::string Navigator::ExecuteCommand(const std::string& command) {
  std::istringstream input(command);
  std::string operation;
  std::string argument;
  std::string trailing;
  input >> operation;

  if (operation == "status" && !(input >> trailing)) {
    return StatusText();
  }
  if (operation == "mode" && !(input >> trailing)) {
    return "OK mode=" + process_manager_.mode() + "\n";
  }
  if (operation == "shutdown" && !(input >> trailing)) {
    stop_requested_ = true;
    return "OK\n";
  }
  if (operation == "reexec" && (input >> argument) && !(input >> trailing)) {
    RunMode mode;
    if (!ParseRunMode(argument, &mode) || config_.FindMode(mode) == nullptr) {
      return "ERROR unknown mode: " + argument + "\n";
    }
    reexec_mode_ = argument;
    stop_requested_ = true;
    return "OK\n";
  }
  if (operation == "reload" && !(input >> trailing)) {
    std::string error;
    return process_manager_.Reload(&error) ? "OK\n" : "ERROR " + error + "\n";
  }
  if (!(input >> argument) || (input >> trailing)) {
    return "ERROR invalid command\n";
  }

  std::string error;
  bool success = false;
  if (operation == "switch") {
    success = process_manager_.SwitchMode(argument, &error);
  } else if (operation == "start") {
    success = process_manager_.StartModule(argument, &error);
  } else if (operation == "stop") {
    success = process_manager_.StopModule(argument, &error);
  } else if (operation == "restart") {
    success = process_manager_.RestartModule(argument, &error);
  } else {
    return "ERROR unknown command\n";
  }
  return success ? "OK\n" : "ERROR " + error + "\n";
}

std::string Navigator::StatusText() const {
  char executable_path[4096];
  const ssize_t executable_size =
      readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1U);
  const std::string executable =
      executable_size > 0 ? std::string(executable_path, static_cast<std::size_t>(executable_size))
                          : "unknown";
  const build::BuildInfo build_info = build::GetBuildInfo();
  std::ostringstream output;
  output << "OK mode=" << process_manager_.mode() << " version=" << build_info.version
         << " commit=" << build_info.git_commit << " executable=" << executable << '\n';
  for (const ProcessStatus& status : process_manager_.Status()) {
    output << "module=" << status.name << " state=" << ToString(status.state)
           << " pid=" << status.pid << " exit=" << status.last_exit_code
           << " restarts=" << status.restart_count << " starts=" << status.start_count
           << " uptime_ms=" << status.uptime_ms << " last_failure_ms=" << status.last_failure_ms
           << " last_signal=" << status.last_signal << '\n';
  }
  return output.str();
}

int RunModuleChild(const std::string& module_name, const std::string& library_path,
                   const std::string& config_path, int ready_fd) {
  signal_received = 0;
  InstallSignalHandlers();

  ModuleLoader loader;
  std::string error;
  if (!loader.Load(library_path, module_name, &error)) {
    LOG_ERROR(error);
    return 65;
  }
  const int start_result = loader.Start(config_path);
  if (start_result != 0) {
    LOG_ERROR("module " + module_name + " failed to start: " + std::to_string(start_result));
    return start_result;
  }
  if (ready_fd >= 0) {
    const char ready = '1';
    if (write(ready_fd, &ready, 1) != 1) {
      close(ready_fd);
      loader.Stop();
      loader.Unload();
      return 74;
    }
    close(ready_fd);
  }

  while (signal_received == 0) {
    const int module_result = loader.Poll();
    if (module_result != 0) {
      loader.Stop();
      loader.Unload();
      return module_result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  loader.Stop();
  loader.Unload();
  return 0;
}

}  // namespace navigator
}  // namespace cockpit
