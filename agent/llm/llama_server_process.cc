#include "agent/llm/llama_server_process.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "cockpit/core/logging/logger.h"

extern char** environ;

namespace cockpit {
namespace voice {
namespace {

constexpr std::chrono::milliseconds kProcessPollInterval{50};
constexpr std::chrono::milliseconds kHealthProbeLimit{200};

class ScopedFd {
 public:
  explicit ScopedFd(int fd = -1) : fd_(fd) {
  }

  ~ScopedFd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  int get() const {
    return fd_;
  }

  void reset(int fd) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_;
};

std::chrono::milliseconds Remaining(std::chrono::steady_clock::time_point deadline) {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return std::chrono::milliseconds::zero();
  }
  return std::max(std::chrono::milliseconds(1),
                  std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
}

std::chrono::milliseconds RestartBackoff(std::chrono::milliseconds initial,
                                         std::chrono::milliseconds maximum, int attempt) {
  const int shift = std::min(std::max(attempt - 1, 0), 10);
  const std::int64_t scaled = initial.count() * (1LL << shift);
  return std::chrono::milliseconds(std::min(scaled, maximum.count()));
}

}  // namespace

LlamaServerProcess::LlamaServerProcess(LlamaServerProcessConfig config, HealthProbe health_probe)
    : config_(std::move(config)),
      health_probe_(health_probe ? std::move(health_probe) : ProbeHttpHealth) {
}

LlamaServerProcess::~LlamaServerProcess() {
  Stop();
}

bool LlamaServerProcess::Start(std::string* error) {
  if (access(config_.executable.c_str(), X_OK) != 0) {
    if (error != nullptr) {
      *error = "llama-server executable is not accessible: " + config_.executable;
    }
    return false;
  }
  if (!std::filesystem::is_regular_file(config_.model_path)) {
    if (error != nullptr) {
      *error = "llama-server model is not a regular file: " + config_.model_path;
    }
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ > 0 || monitor_.joinable()) {
      if (error != nullptr) {
        *error = "llama-server process is already started";
      }
      return false;
    }
    stopping_ = false;
    ready_ = false;
    restart_count_ = 0;
    last_error_.clear();
  }

  std::string start_error;
  if (!SpawnAndWaitUntilReady(&start_error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = start_error;
    if (error != nullptr) {
      *error = start_error;
    }
    return false;
  }
  monitor_ = std::thread(&LlamaServerProcess::MonitorLoop, this);
  return true;
}

void LlamaServerProcess::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  changed_.notify_all();
  if (monitor_.joinable()) {
    monitor_.join();
  }

  pid_t pid = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pid = pid_;
    pid_ = 0;
    ready_ = false;
  }
  if (pid > 0) {
    TerminateAndReap(pid);
  }
  changed_.notify_all();
}

pid_t LlamaServerProcess::pid() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pid_;
}

bool LlamaServerProcess::ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ready_;
}

std::uint64_t LlamaServerProcess::restart_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return restart_count_;
}

std::string LlamaServerProcess::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

bool LlamaServerProcess::WaitForRestartCount(std::uint64_t count,
                                             std::chrono::milliseconds timeout) const {
  std::unique_lock<std::mutex> lock(mutex_);
  return changed_.wait_for(lock, timeout, [this, count] {
    return restart_count_ >= count && ready_;
  });
}

bool LlamaServerProcess::SpawnAndWaitUntilReady(std::string* error) {
  pid_t child_pid = 0;
  if (!Spawn(&child_pid, error)) {
    return false;
  }
  bool startup_cancelled = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    startup_cancelled = stopping_;
    if (!startup_cancelled) {
      pid_ = child_pid;
      ready_ = false;
    }
  }
  if (startup_cancelled) {
    TerminateAndReap(child_pid);
    if (error != nullptr) {
      *error = "llama-server startup cancelled";
    }
    return false;
  }
  changed_.notify_all();
  return WaitUntilReady(child_pid, error);
}

bool LlamaServerProcess::Spawn(pid_t* pid, std::string* error) const {
  std::vector<std::string> arguments{
      "/usr/bin/setpriv",
      "--pdeathsig",
      "SIGTERM",
      "--",
      config_.executable,
      "--host",
      config_.host,
      "--port",
      std::to_string(config_.port),
      "--model",
      config_.model_path,
      "--alias",
      config_.model_alias,
      "--ctx-size",
      std::to_string(config_.context_size),
      "--parallel",
      "1",
      "--no-webui",
  };
  if (config_.gpu_layers != 0) {
    arguments.emplace_back("--n-gpu-layers");
    arguments.push_back(std::to_string(config_.gpu_layers));
  }
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1U);
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  posix_spawnattr_t attributes;
  const int attributes_result = posix_spawnattr_init(&attributes);
  int configuration_result = attributes_result;
  if (configuration_result == 0) {
    configuration_result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  }
  if (configuration_result == 0) {
    configuration_result = posix_spawnattr_setpgroup(&attributes, 0);
  }
  const int spawn_result =
      configuration_result == 0
          ? posix_spawn(pid, arguments.front().c_str(), nullptr, &attributes, argv.data(), environ)
          : configuration_result;
  if (attributes_result == 0) {
    posix_spawnattr_destroy(&attributes);
  }
  if (spawn_result != 0) {
    if (error != nullptr) {
      *error = "failed to spawn llama-server: " + std::string(std::strerror(spawn_result));
    }
    return false;
  }
  return true;
}

bool LlamaServerProcess::WaitUntilReady(pid_t pid, std::string* error) {
  const auto deadline = std::chrono::steady_clock::now() + config_.startup_timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    bool startup_cancelled = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      startup_cancelled = stopping_;
    }
    if (startup_cancelled) {
      TerminateAndReap(pid);
      std::lock_guard<std::mutex> lock(mutex_);
      if (pid_ == pid) {
        pid_ = 0;
      }
      if (error != nullptr) {
        *error = "llama-server startup cancelled";
      }
      return false;
    }

    int wait_status = 0;
    const pid_t wait_result = waitpid(pid, &wait_status, WNOHANG);
    if (wait_result == pid) {
      TerminateAndReap(pid);
      std::lock_guard<std::mutex> lock(mutex_);
      if (pid_ == pid) {
        pid_ = 0;
      }
      if (error != nullptr) {
        *error = "llama-server exited before readiness";
      }
      return false;
    }
    if (wait_result < 0 && errno != EINTR) {
      const int wait_error = errno;
      if (wait_error != ECHILD) {
        TerminateAndReap(pid);
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (pid_ == pid) {
        pid_ = 0;
      }
      if (error != nullptr) {
        *error = "failed while waiting for llama-server readiness: " +
                 std::string(std::strerror(wait_error));
      }
      return false;
    }

    const auto probe_timeout = std::min(kHealthProbeLimit, Remaining(deadline));
    if (health_probe_(config_.host, config_.port, probe_timeout)) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (pid_ == pid && !stopping_) {
        ready_ = true;
        last_error_.clear();
        changed_.notify_all();
        return true;
      }
    }

    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock, kProcessPollInterval, [this] {
      return stopping_;
    });
  }

  TerminateAndReap(pid);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ == pid) {
      pid_ = 0;
      ready_ = false;
    }
  }
  if (error != nullptr) {
    *error = "llama-server readiness timed out";
  }
  return false;
}

void LlamaServerProcess::MonitorLoop() {
  bool restart_needed = false;
  int restart_attempts = 0;
  int health_failures = 0;
  auto next_health_check = std::chrono::steady_clock::now() + config_.health_check_interval;
  while (true) {
    if (restart_needed) {
      if (restart_attempts > config_.restart_limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "llama-server restart limit exceeded";
        LOG_ERROR(last_error_);
        return;
      }
      const auto restart_delay =
          RestartBackoff(config_.restart_delay, config_.restart_max_delay, restart_attempts);
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (changed_.wait_for(lock, restart_delay, [this] {
              return stopping_;
            })) {
          return;
        }
      }
      std::string restart_error;
      if (!SpawnAndWaitUntilReady(&restart_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
          return;
        }
        last_error_ = restart_error;
        ++restart_attempts;
        LOG_ERROR("failed to restart llama-server: " + restart_error);
        continue;
      }
      restart_needed = false;
      health_failures = 0;
      next_health_check = std::chrono::steady_clock::now() + config_.health_check_interval;
      LOG_INFO("restarted llama-server pid=" + std::to_string(pid()));
      continue;
    }

    pid_t child_pid = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait_for(lock, kProcessPollInterval, [this] {
        return stopping_;
      });
      if (stopping_) {
        return;
      }
      child_pid = pid_;
    }
    if (child_pid <= 0) {
      continue;
    }

    int wait_status = 0;
    const pid_t wait_result = waitpid(child_pid, &wait_status, WNOHANG);
    if (wait_result == 0) {
      const auto now = std::chrono::steady_clock::now();
      if (now < next_health_check) {
        continue;
      }
      next_health_check = now + config_.health_check_interval;
      if (health_probe_(config_.host, config_.port, kHealthProbeLimit)) {
        health_failures = 0;
        restart_attempts = 0;
        continue;
      }
      ++health_failures;
      if (health_failures < config_.health_failure_threshold) {
        continue;
      }
      TerminateAndReap(child_pid);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pid_ == child_pid) {
          pid_ = 0;
          ready_ = false;
          ++restart_count_;
          last_error_ = "llama-server health check failed";
        }
      }
      changed_.notify_all();
      LOG_ERROR("llama-server health check failed; scheduling restart");
      ++restart_attempts;
      restart_needed = true;
      continue;
    }
    if (wait_result < 0 && errno == EINTR) {
      continue;
    }
    if (wait_result < 0 && errno != ECHILD) {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = "failed to monitor llama-server: " + std::string(std::strerror(errno));
      continue;
    }

    TerminateAndReap(child_pid);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (pid_ == child_pid) {
        pid_ = 0;
        ready_ = false;
        ++restart_count_;
        last_error_ = "llama-server exited unexpectedly";
      }
    }
    changed_.notify_all();
    LOG_ERROR("llama-server exited unexpectedly; scheduling restart");
    ++restart_attempts;
    restart_needed = true;
  }
}

void LlamaServerProcess::TerminateAndReap(pid_t pid) const {
  if (pid <= 0) {
    return;
  }
  if (kill(-pid, SIGTERM) < 0 && errno != ESRCH) {
    LOG_ERROR("failed to terminate llama-server process group: " +
              std::string(std::strerror(errno)));
  }
  const auto deadline = std::chrono::steady_clock::now() + config_.shutdown_timeout;
  int wait_status = 0;
  bool leader_reaped = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!leader_reaped) {
      const pid_t wait_result = waitpid(pid, &wait_status, WNOHANG);
      if (wait_result == pid || (wait_result < 0 && errno == ECHILD)) {
        leader_reaped = true;
      } else if (wait_result < 0 && errno != EINTR) {
        break;
      }
    }
    errno = 0;
    const bool process_group_gone = kill(-pid, 0) < 0 && errno == ESRCH;
    if (leader_reaped && process_group_gone) {
      return;
    }
    std::this_thread::sleep_for(kProcessPollInterval);
  }
  if (kill(-pid, SIGKILL) < 0 && errno != ESRCH) {
    LOG_ERROR("failed to kill llama-server process group: " + std::string(std::strerror(errno)));
  }
  while (!leader_reaped && waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
  }
}

bool LlamaServerProcess::ProbeHttpHealth(const std::string& host, std::uint16_t port,
                                         std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;
  addrinfo* addresses = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addresses) != 0 ||
      addresses == nullptr) {
    return false;
  }

  ScopedFd socket;
  for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
    socket.reset(
        ::socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC, address->ai_protocol));
    if (socket.get() < 0) {
      continue;
    }
    const int flags = fcntl(socket.get(), F_GETFL, 0);
    if (flags < 0 || fcntl(socket.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
      continue;
    }
    if (::connect(socket.get(), address->ai_addr, address->ai_addrlen) == 0 ||
        errno == EINPROGRESS) {
      pollfd descriptor{socket.get(), POLLOUT, 0};
      if (poll(&descriptor, 1, static_cast<int>(Remaining(deadline).count())) > 0) {
        int socket_error = 0;
        socklen_t socket_error_size = sizeof(socket_error);
        if (getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) ==
                0 &&
            socket_error == 0) {
          break;
        }
      }
    }
    socket.reset(-1);
  }
  freeaddrinfo(addresses);
  if (socket.get() < 0) {
    return false;
  }
  if (std::chrono::steady_clock::now() >= deadline) {
    return false;
  }

  constexpr char request[] = "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  if (send(socket.get(), request, sizeof(request) - 1U, MSG_NOSIGNAL) < 0) {
    return false;
  }
  pollfd descriptor{socket.get(), POLLIN, 0};
  if (poll(&descriptor, 1, static_cast<int>(Remaining(deadline).count())) <= 0) {
    return false;
  }
  char response[64]{};
  const ssize_t received = recv(socket.get(), response, sizeof(response) - 1U, 0);
  if (received <= 0) {
    return false;
  }
  const std::string status(response, static_cast<std::size_t>(received));
  return status.rfind("HTTP/1.1 200", 0) == 0 || status.rfind("HTTP/1.0 200", 0) == 0;
}

}  // namespace voice
}  // namespace cockpit
