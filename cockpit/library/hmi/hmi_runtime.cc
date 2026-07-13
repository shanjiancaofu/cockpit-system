#include "cockpit/library/hmi/hmi_runtime.h"

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"

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
                        config.logging().max_bytes, config.logging().mirror_stderr);

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

    int exec_pipe[2];
    if (pipe2(exec_pipe, O_CLOEXEC) != 0) {
      LOG_ERROR("failed to create cockpit-ui exec pipe: " + std::string(std::strerror(errno)));
      return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
      const int fork_error = errno;
      close(exec_pipe[0]);
      close(exec_pipe[1]);
      LOG_ERROR("failed to fork cockpit-ui: " + std::string(std::strerror(fork_error)));
      return false;
    }
    if (pid == 0) {
      close(exec_pipe[0]);
      const pid_t parent_pid = getppid();
      if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0) {
        const int child_error = errno;
        const ssize_t write_result = write(exec_pipe[1], &child_error, sizeof(child_error));
        (void)write_result;
        _exit(126);
      }
      if (getppid() != parent_pid) {
        const int child_error = ECHILD;
        const ssize_t write_result = write(exec_pipe[1], &child_error, sizeof(child_error));
        (void)write_result;
        _exit(126);
      }
      execl(ui_path.c_str(), ui_path.c_str(), "--config", config_path.c_str(),
            static_cast<char*>(nullptr));
      const int exec_error = errno;
      const ssize_t write_result = write(exec_pipe[1], &exec_error, sizeof(exec_error));
      (void)write_result;
      _exit(127);
    }

    close(exec_pipe[1]);
    int exec_error = 0;
    ssize_t read_size;
    do {
      read_size = read(exec_pipe[0], &exec_error, sizeof(exec_error));
    } while (read_size < 0 && errno == EINTR);
    const int read_error = errno;
    close(exec_pipe[0]);
    if (read_size != 0) {
      if (read_size < 0) {
        kill(pid, SIGKILL);
      }
      int wait_status = 0;
      while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
      }
      if (read_size > 0) {
        LOG_ERROR("failed to execute cockpit-ui: " + std::string(std::strerror(exec_error)));
      } else {
        LOG_ERROR("failed to read cockpit-ui exec status: " +
                  std::string(std::strerror(read_error)));
      }
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
