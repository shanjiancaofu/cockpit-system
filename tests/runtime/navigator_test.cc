#include "cockpit/navigator/navigator.h"

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/core/json/json.h"
#include "cockpit/navigator/connection/ipc_connector.h"
#include "cockpit/navigator/run_config/run_config.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

bool Send(const std::string& socket_path, const std::string& command, std::string* response) {
  std::string error;
  return cockpit::navigator::IpcConnector::SendRequest(socket_path, command, response, &error);
}

bool WaitFor(const std::string& socket_path, const std::string& expected,
             std::string* last_status) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (Send(socket_path, "status", last_status) &&
        last_status->find(expected) != std::string::npos) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

pid_t ModulePid(const std::string& status, const std::string& module) {
  const std::string prefix = "module=" + module + " ";
  const std::size_t module_position = status.find(prefix);
  if (module_position == std::string::npos) {
    return 0;
  }
  const std::size_t pid_position = status.find("pid=", module_position + prefix.size());
  if (pid_position == std::string::npos) {
    return 0;
  }
  return static_cast<pid_t>(std::strtol(status.c_str() + pid_position + 4, nullptr, 10));
}

bool WaitForExit(pid_t pid, int* status) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(pid, status, WNOHANG);
    if (result == pid) {
      return true;
    }
    if (result < 0) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

int RunModuleChild(const std::string& navigator_path, const std::string& module_path) {
  const pid_t pid = fork();
  if (pid == 0) {
    execl(navigator_path.c_str(), navigator_path.c_str(), "--module-child", "--module",
          "incompatible", "--library", module_path.c_str(), "--module-config", "",
          static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "expected navigator, module and cockpit-ctl paths\n";
    return 1;
  }

  const std::string navigator_path = argv[1];
  const std::string good_module = argv[2];
  const std::string crash_module = argv[3];
  const std::string incompatible_module = argv[4];
  const std::string cockpit_ctl_path = argv[5];
  const std::filesystem::path module_directory = std::filesystem::path(good_module).parent_path();
  const std::string good_library = std::filesystem::path(good_module).filename().string();
  const std::string crash_library = std::filesystem::path(crash_module).filename().string();
  const std::string incompatible_library =
      std::filesystem::path(incompatible_module).filename().string();
  const std::filesystem::path test_dir = std::filesystem::temp_directory_path() /
                                         ("cockpit-navigator-test-" + std::to_string(getpid()));
  std::filesystem::create_directories(test_dir);
  const std::string socket_path = (test_dir / "navigator.sock").string();

  std::ofstream socket_blocker(socket_path);
  socket_blocker << "do not remove";
  socket_blocker.close();
  cockpit::navigator::IpcConnector connector;
  std::string socket_error;
  bool success = true;
  success &=
      Expect(!connector.Open(socket_path, &socket_error), "navigator replaced a non-socket path");
  success &=
      Expect(std::filesystem::is_regular_file(socket_path), "navigator removed a non-socket path");
  std::filesystem::remove(socket_path);

  success &= Expect(connector.Open(socket_path, &socket_error), "failed to open IPC connector");
  cockpit::navigator::IpcConnector second_connector;
  std::string second_error;
  success &= Expect(!second_connector.Open(socket_path, &second_error) &&
                        second_error.find("another Navigator") != std::string::npos,
                    "second Navigator acquired the active socket");

  std::string fragmented_response;
  std::thread fragmented_client([&socket_path, &fragmented_response]() {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (fd < 0 || connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      if (fd >= 0) {
        close(fd);
      }
      return;
    }
    send(fd, "sta", 3, MSG_NOSIGNAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    send(fd, "tus\n", 4, MSG_NOSIGNAL);
    shutdown(fd, SHUT_WR);
    char buffer[32];
    const ssize_t size = read(fd, buffer, sizeof(buffer));
    if (size > 0) {
      fragmented_response.assign(buffer, static_cast<std::size_t>(size));
    }
    close(fd);
  });
  std::string fragmented_request;
  const int fragmented_fd = connector.WaitForRequest(1000, &fragmented_request);
  success &= Expect(fragmented_fd >= 0 && fragmented_request == "status",
                    "fragmented IPC request was not reassembled");
  if (fragmented_fd >= 0) {
    connector.ReplyAndClose(fragmented_fd, "OK\n");
  }
  fragmented_client.join();
  success &= Expect(fragmented_response == "OK\n", "fragmented IPC client received no reply");

  std::string oversized_response;
  std::thread oversized_client([&socket_path, &oversized_response]() {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (fd < 0 || connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      if (fd >= 0) {
        close(fd);
      }
      return;
    }
    std::string request(64U * 1024U + 1U, 'x');
    request.push_back('\n');
    std::size_t offset = 0;
    while (offset < request.size()) {
      const ssize_t written =
          send(fd, request.data() + offset, request.size() - offset, MSG_NOSIGNAL);
      if (written <= 0) {
        close(fd);
        return;
      }
      offset += static_cast<std::size_t>(written);
    }
    shutdown(fd, SHUT_WR);
    char buffer[128];
    const ssize_t size = read(fd, buffer, sizeof(buffer));
    if (size > 0) {
      oversized_response.assign(buffer, static_cast<std::size_t>(size));
    }
    close(fd);
  });
  std::string oversized_request;
  success &= Expect(connector.WaitForRequest(1000, &oversized_request) < 0,
                    "oversized IPC request was accepted");
  oversized_client.join();
  success &= Expect(oversized_response == "ERROR request exceeds 64 KiB\n",
                    "oversized IPC request did not receive an explicit error");
  connector.Close();

  success &= Expect(connector.Open(socket_path, &socket_error), "failed to open stalled peer");
  std::thread stalled_peer([&connector]() {
    std::string request;
    const int client_fd = connector.WaitForRequest(1000, &request);
    if (client_fd >= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      close(client_fd);
    }
  });
  std::string stalled_response;
  std::string stalled_error;
  const bool stalled_result = cockpit::navigator::IpcConnector::SendRequest(
      socket_path, "status", &stalled_response, &stalled_error);
  stalled_peer.join();
  success &=
      Expect(!stalled_result && stalled_error == "timed out waiting for Unix socket response",
             "stalled peer did not time out");
  connector.Close();

  cockpit::navigator::RunConfig config;
  config.initial_mode = cockpit::navigator::RunMode::kNormal;
  config.socket_path = socket_path;
  config.startup_timeout_ms = 500;
  config.stop_timeout_ms = 500;
  config.restart_window_seconds = 10;
  config.modules = {
      {cockpit::navigator::ModuleId::kTransfer, good_library},
      {cockpit::navigator::ModuleId::kDebugger, crash_library, false, 2, 20},
      {cockpit::navigator::ModuleId::kCalibration, incompatible_library},
  };
  config.modes = {
      {cockpit::navigator::RunMode::kNormal, {cockpit::navigator::ModuleId::kTransfer}},
      {cockpit::navigator::RunMode::kDevelopment,
       {cockpit::navigator::ModuleId::kTransfer, cockpit::navigator::ModuleId::kDebugger}},
      {cockpit::navigator::RunMode::kUpgrade, {cockpit::navigator::ModuleId::kCalibration}},
  };

  const pid_t navigator_pid = fork();
  if (navigator_pid == 0) {
    cockpit::navigator::Navigator navigator(std::move(config), navigator_path,
                                            module_directory.string(), "",
                                            (test_dir / "crashes").string());
    _exit(navigator.Run());
  }

  std::string response;
  success &= Expect(WaitFor(socket_path, "module=transfer state=running", &response),
                    "normal mode did not start transfer module");
  success &= Expect(response.find("starts=1") != std::string::npos &&
                        response.find("uptime_ms=") != std::string::npos,
                    "module lifecycle fields are missing");
  const pid_t transfer_pid = ModulePid(response, "transfer");
  std::ifstream process_name("/proc/" + std::to_string(transfer_pid) + "/comm");
  std::string process_name_value;
  std::getline(process_name, process_name_value);
  success &= Expect(transfer_pid > 0 && process_name_value == "transfer",
                    "module process name is not observable");
  success &= Expect(Send(socket_path, "reload", &response) && response == "OK\n",
                    "failed to reload active mode");
  success &= Expect(WaitFor(socket_path, "module=transfer state=running", &response),
                    "reloaded module is not running");
  const pid_t ctl_pid = fork();
  if (ctl_pid == 0) {
    execl(cockpit_ctl_path.c_str(), cockpit_ctl_path.c_str(), "runtime", "mode", "--socket",
          socket_path.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int ctl_status = 0;
  while (waitpid(ctl_pid, &ctl_status, 0) < 0) {
  }
  success &= Expect(WIFEXITED(ctl_status) && WEXITSTATUS(ctl_status) == 0,
                    "cockpit-ctl runtime mode failed");
  success &= Expect(Send(socket_path, "switch development", &response) && response == "OK\n",
                    "failed to switch to development mode");
  success &= Expect(WaitFor(socket_path, "module=debugger state=failed", &response),
                    "crashing module did not reach failed state");
  success &= Expect(response.find("restarts=2") != std::string::npos,
                    "crashing module restart count mismatch");
  success &= Expect(response.find("starts=3") != std::string::npos &&
                        response.find("last_signal=11") != std::string::npos,
                    "crashing module lifecycle counters mismatch");
  std::size_t crash_report_count = 0;
  bool found_restart_success = false;
  bool found_restart_limit = false;
  const std::filesystem::path crash_directory = test_dir / "crashes";
  success &=
      Expect(std::filesystem::is_directory(crash_directory), "crash report directory is missing");
  if (std::filesystem::is_directory(crash_directory)) {
    for (const auto& entry : std::filesystem::directory_iterator(crash_directory)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") {
        continue;
      }
      ++crash_report_count;
      std::ifstream input(entry.path());
      const std::string report((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
      success &= Expect(cockpit::json::IsValidValue(report), "crash report is not valid JSON");
      success &= Expect(report.find("\"module\":\"debugger\"") != std::string::npos,
                        "crash report module mismatch");
      success &= Expect(report.find("\"termination\":\"signal\"") != std::string::npos &&
                            report.find("\"signal\":11") != std::string::npos,
                        "crash report signal mismatch");
      found_restart_success |= report.find("\"restart_result\":\"succeeded\"") != std::string::npos;
      found_restart_limit |=
          report.find("\"restart_result\":\"limit_exceeded\"") != std::string::npos;
    }
  }
  success &= Expect(crash_report_count == 3, "unexpected crash report count");
  success &= Expect(found_restart_success, "successful crash restart was not recorded");
  success &= Expect(found_restart_limit, "restart limit was not recorded");
  success &= Expect(Send(socket_path, "restart transfer", &response) && response == "OK\n",
                    "failed to restart transfer module");
  success &= Expect(WaitFor(socket_path, "module=transfer state=running", &response),
                    "restarted module is not running");
  success &= Expect(Send(socket_path, "switch normal", &response) && response == "OK\n",
                    "failed to return to normal mode");
  success &= Expect(WaitFor(socket_path, "module=debugger state=stopped", &response),
                    "mode switch did not stop crash module");
  success &= Expect(Send(socket_path, "switch upgrade", &response) &&
                        response.find("ERROR module calibration failed before ready") == 0,
                    "pre-ready module failure was not reported");
  success &= Expect(WaitFor(socket_path, "module=transfer state=running", &response),
                    "failed mode switch did not restore previous modules");
  success &= Expect(response.find("OK mode=normal") == 0, "failed mode switch changed active mode");
  success &=
      Expect(Send(socket_path, "unknown", &response) && response == "ERROR invalid command\n",
             "invalid command was accepted");
  success &= Expect(RunModuleChild(navigator_path, incompatible_module) == 65,
                    "incompatible ABI was not rejected");

  Send(socket_path, "shutdown", &response);
  int navigator_status = 0;
  while (waitpid(navigator_pid, &navigator_status, 0) < 0) {
  }
  success &= Expect(WIFEXITED(navigator_status) && WEXITSTATUS(navigator_status) == 0,
                    "navigator did not exit cleanly");
  std::size_t final_crash_report_count = 0;
  if (std::filesystem::is_directory(crash_directory)) {
    for (const auto& entry : std::filesystem::directory_iterator(crash_directory)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        ++final_crash_report_count;
      }
    }
  }
  success &= Expect(final_crash_report_count == crash_report_count,
                    "clean shutdown unexpectedly created a crash report");

  cockpit::navigator::RunConfig critical_config;
  critical_config.initial_mode = cockpit::navigator::RunMode::kNormal;
  critical_config.socket_path = (test_dir / "critical.sock").string();
  critical_config.startup_timeout_ms = 500;
  critical_config.stop_timeout_ms = 500;
  critical_config.restart_window_seconds = 10;
  critical_config.modules = {
      {cockpit::navigator::ModuleId::kDebugger, crash_library, true, 0, 0},
  };
  critical_config.modes = {
      {cockpit::navigator::RunMode::kNormal, {cockpit::navigator::ModuleId::kDebugger}},
  };
  const pid_t critical_pid = fork();
  if (critical_pid == 0) {
    cockpit::navigator::Navigator navigator(std::move(critical_config), navigator_path,
                                            module_directory.string(), "",
                                            (test_dir / "critical-crashes").string());
    _exit(navigator.Run());
  }
  int critical_status = 0;
  const bool critical_exited = WaitForExit(critical_pid, &critical_status);
  if (!critical_exited) {
    kill(critical_pid, SIGKILL);
    waitpid(critical_pid, &critical_status, 0);
  }
  success &=
      Expect(critical_exited && WIFEXITED(critical_status) && WEXITSTATUS(critical_status) == 1,
             "critical module failure did not stop Navigator");

  std::filesystem::remove_all(test_dir);
  return success ? 0 : 1;
}
