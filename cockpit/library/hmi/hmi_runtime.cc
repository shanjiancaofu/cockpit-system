#include "cockpit/library/hmi/hmi_runtime.h"

#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"

extern char** environ;

namespace cockpit {
namespace hmi {

HmiRuntime::~HmiRuntime() {
  Stop();
}

bool HmiRuntime::Start(const std::string& config_path) {
  if (pid_ != 0) {
    return false;
  }

  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("hmi", config.paths().log_dir, logging::ParseLevel(config.logging().level),
                        config.logging().mirror_stderr, config.logging().dump_time_secs,
                        config.logging().cut_off_time_mins, config.logging().max_files);

    char executable_path[PATH_MAX];
    const ssize_t path_size =
        readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
    if (path_size <= 0) {
      LOG_ERROR("failed to resolve Navigator executable path");
      return false;
    }
    executable_path[path_size] = '\0';
    const std::string ui_path =
        (std::filesystem::path(executable_path).parent_path() / "cockpit-ui").string();
    if (access(ui_path.c_str(), X_OK) != 0) {
      LOG_ERROR("cockpit-ui executable is unavailable: " + ui_path);
      return false;
    }

    std::vector<std::string> arguments{"/usr/bin/setpriv", "--pdeathsig", "SIGTERM", "--", ui_path,
                                       "--config",         config_path};
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string& argument : arguments) {
      argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawn_result =
        posix_spawn(&pid, arguments.front().c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawn_result != 0) {
      LOG_ERROR("failed to spawn cockpit-ui: " + std::string(std::strerror(spawn_result)));
      return false;
    }

    pid_ = pid;
    result_ = 0;
    LOG_INFO("started cockpit-ui pid=" + std::to_string(pid_));
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to start hmi: " + std::string(error.what()));
    return false;
  }
}

void HmiRuntime::Stop() {
  if (pid_ == 0) {
    return;
  }

  kill(pid_, SIGTERM);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  int wait_status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t wait_result = waitpid(pid_, &wait_status, WNOHANG);
    if (wait_result == pid_ || (wait_result < 0 && errno == ECHILD)) {
      pid_ = 0;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  kill(pid_, SIGKILL);
  while (waitpid(pid_, &wait_status, 0) < 0 && errno == EINTR) {
  }
  pid_ = 0;
}

int HmiRuntime::Poll() {
  if (pid_ == 0) {
    return result_ == 0 ? 1 : result_;
  }

  int wait_status = 0;
  const pid_t wait_result = waitpid(pid_, &wait_status, WNOHANG);
  if (wait_result == 0 || (wait_result < 0 && errno == EINTR)) {
    return 0;
  }
  if (wait_result < 0) {
    result_ = 1;
  } else if (WIFEXITED(wait_status)) {
    result_ = WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    result_ = 128 + WTERMSIG(wait_status);
  } else {
    return 0;
  }
  if (result_ == 0) {
    result_ = 1;
  }
  pid_ = 0;
  LOG_ERROR("cockpit-ui exited unexpectedly result=" + std::to_string(result_));
  return result_;
}

}  // namespace hmi
}  // namespace cockpit
