#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/navigator/connection/ipc_connector.h"

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
  if (argc != 5) {
    std::cerr << "expected navigator, good, crash and incompatible module paths\n";
    return 1;
  }

  const std::string navigator_path = argv[1];
  const std::string good_module = argv[2];
  const std::string crash_module = argv[3];
  const std::string incompatible_module = argv[4];
  const std::filesystem::path test_dir = std::filesystem::temp_directory_path() /
                                         ("cockpit-navigator-test-" + std::to_string(getpid()));
  std::filesystem::create_directories(test_dir);
  const std::string socket_path = (test_dir / "navigator.sock").string();
  const std::string config_path = (test_dir / "navigator.yaml").string();

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

  std::ofstream config(config_path);
  config << "initial_mode: normal\n"
         << "socket_path: " << socket_path << "\n"
         << "startup_timeout_ms: 500\n"
         << "stop_timeout_ms: 500\n"
         << "restart:\n"
         << "  max_attempts: 2\n"
         << "  window_seconds: 10\n"
         << "modules:\n"
         << "  - name: transfer\n"
         << "    library: " << good_module << "\n"
         << "  - name: crash\n"
         << "    library: " << crash_module << "\n"
         << "  - name: incompatible\n"
         << "    library: " << incompatible_module << "\n"
         << "modes:\n"
         << "  normal: [transfer]\n"
         << "  development: [transfer, crash]\n"
         << "  broken: [incompatible]\n";
  config.close();

  const pid_t navigator_pid = fork();
  if (navigator_pid == 0) {
    execl(navigator_path.c_str(), navigator_path.c_str(), "--config", config_path.c_str(),
          "--module-dir", test_dir.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  std::string response;
  success &= Expect(WaitFor(socket_path, "module=transfer state=running", &response),
                    "normal mode did not start transfer module");
  success &= Expect(Send(socket_path, "switch development", &response) && response == "OK\n",
                    "failed to switch to development mode");
  success &= Expect(WaitFor(socket_path, "module=crash state=failed", &response),
                    "crashing module did not reach failed state");
  success &= Expect(response.find("restarts=2") != std::string::npos,
                    "crashing module restart count mismatch");
  success &= Expect(Send(socket_path, "restart transfer", &response) && response == "OK\n",
                    "failed to restart transfer module");
  success &= Expect(WaitFor(socket_path, "module=transfer state=running", &response),
                    "restarted module is not running");
  success &= Expect(Send(socket_path, "switch normal", &response) && response == "OK\n",
                    "failed to return to normal mode");
  success &= Expect(WaitFor(socket_path, "module=crash state=stopped", &response),
                    "mode switch did not stop crash module");
  success &= Expect(Send(socket_path, "switch broken", &response) &&
                        response.find("ERROR module incompatible failed before ready") == 0,
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
  std::filesystem::remove_all(test_dir);
  return success ? 0 : 1;
}
