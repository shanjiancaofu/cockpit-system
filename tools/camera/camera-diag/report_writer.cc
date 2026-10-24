#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include "diagnostics.h"

#include "cockpit/exchange/foundation/json/json.h"

namespace cockpit::camera_diag {
Json Quote(const std::string& value) {
  return "\"" + json::EscapeString(value) + "\"";
}
Json Object(std::initializer_list<std::pair<std::string, Json>> fields) {
  std::string out = "{";
  for (const auto& field : fields) {
    if (out.size() > 1) out += ',';
    out += Quote(field.first) + ":" + field.second;
  }
  return out + "}";
}
Json Array(const std::vector<Json>& values) {
  std::string out = "[";
  for (const auto& value : values) {
    if (out.size() > 1) out += ',';
    out += value;
  }
  return out + "]";
}
Json Strings(const std::vector<std::string>& values) {
  std::vector<Json> quoted;
  for (const auto& value : values) quoted.push_back(Quote(value));
  return Array(quoted);
}
bool WriteReport(const std::string& path, const Json& report) {
  std::ofstream out(path);
  out << report << '\n';
  out.close();
  return static_cast<bool>(out);
}
std::string ReadText(const std::string& path) {
  std::ifstream in(path);
  std::string result{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  while (!result.empty() && (result.back() == '\0' || result.back() == '\n')) result.pop_back();
  return result;
}
std::vector<std::string> DevicePaths(const std::string& prefix) {
  std::vector<std::string> paths;
  for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
    const auto name = entry.path().filename().string();
    if (name.compare(0, prefix.size(), prefix) == 0 && name.size() > prefix.size() &&
        name.find_first_not_of("0123456789", prefix.size()) == std::string::npos)
      paths.push_back(entry.path().string());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}
int Ioctl(int fd, unsigned long request, void* value) {
  int result;
  do {
    result = ioctl(fd, request, value);
  } while (result < 0 && errno == EINTR);
  return result;
}
CommandResult RunCommand(const std::vector<std::string>& args, int timeout_ms) {
  CommandResult result;
  if (args.empty()) return result;
  int pipes[2];
  if (pipe2(pipes, O_CLOEXEC) != 0) return result;
  Fd read_fd(pipes[0]), write_fd(pipes[1]);
  std::vector<char*> argv;
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  const pid_t pid = fork();
  if (pid < 0) return result;
  if (pid == 0) {
    setpgid(0, 0);
    dup2(write_fd.value, STDOUT_FILENO);
    dup2(write_fd.value, STDERR_FILENO);
    close(read_fd.value);
    close(write_fd.value);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  setpgid(pid, pid);
  close(write_fd.value);
  write_fd.value = -1;
  fcntl(read_fd.value, F_SETFL, O_NONBLOCK);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  int status = 0;
  const auto append = [&](const char* data, ssize_t count) {
    constexpr std::size_t kLimit = 4 * 1024 * 1024;
    const auto remaining = kLimit - result.output.size();
    const auto bytes = std::min(remaining, static_cast<std::size_t>(count));
    result.output.append(data, bytes);
    result.truncated = result.truncated || bytes != static_cast<std::size_t>(count);
  };
  for (;;) {
    char buffer[4096];
    ssize_t count;
    while ((count = read(read_fd.value, buffer, sizeof(buffer))) > 0) {
      append(buffer, count);
    }
    const auto waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      // Drain any bytes written between the last read and child exit.
      while ((count = read(read_fd.value, buffer, sizeof(buffer))) > 0) append(buffer, count);
      result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      break;
    }
    if (waited < 0 && errno != EINTR) break;
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      result.code = 124;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return result;
}
}  // namespace cockpit::camera_diag
