#include "tools/safe-ota/action/safe_ota.h"

#include <fcntl.h>
#include <sys/file.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/library/upgrader/upgrade_transaction.h"
#include "cockpit/navigator/connection/ipc_connector.h"

namespace cockpit {
namespace safe_ota {
namespace {

constexpr int kRuntimeCommandTimeoutMs = 30000;
constexpr int kUpgradeResultTimeoutSeconds = 600;

bool SendRuntimeCommand(const std::string& socket_path, const std::string& command,
                        std::string* error) {
  std::string response;
  if (!navigator::IpcConnector::SendRequest(socket_path, command, &response, error,
                                            kRuntimeCommandTimeoutMs)) {
    return false;
  }
  if (response.rfind("OK", 0) != 0) {
    *error = response;
    return false;
  }
  return true;
}

bool WaitForResult(const std::filesystem::path& path, int timeout_seconds,
                   upgrader::UpgradeResult* result, std::string* error) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::is_regular_file(path)) {
      return upgrader::LoadUpgradeResult(path, result, error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  *error = "upgrader did not publish a result before timeout";
  return false;
}

bool WaitForRuntime(const std::string& socket_path, const std::filesystem::path& install_root,
                    const std::string& version, int timeout_seconds, std::string* error) {
  const std::string expected_executable =
      std::filesystem::weakly_canonical(install_root / "current/bin/cockpit-navigator").string();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  std::string last_error;
  while (std::chrono::steady_clock::now() < deadline) {
    std::string response;
    if (navigator::IpcConnector::SendRequest(socket_path, "status", &response, &last_error, 500) &&
        response.rfind("OK", 0) == 0 &&
        response.find(" executable=" + expected_executable + "\n") != std::string::npos) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  *error = "replacement Navigator did not become ready with version " + version;
  if (!last_error.empty()) {
    *error += ": " + last_error;
  }
  return false;
}

bool IsWithinDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
  const std::filesystem::path relative = path.lexically_relative(directory);
  return !relative.empty() && relative != "." && !relative.is_absolute() &&
         *relative.begin() != "..";
}

}  // namespace

diagnostics::ExitCode ExecuteSafeOta(const SafeOtaOptions& options) {
  using diagnostics::ExitCode;

  std::error_code filesystem_error;
  std::filesystem::create_directories(options.install_root / "run", filesystem_error);
  if (filesystem_error) {
    std::cerr << "create upgrade run directory failed: " << filesystem_error.message() << '\n';
    return ExitCode::kOperationFailed;
  }
  const int lock_fd = open((options.install_root / "run/safe-ota.lock").c_str(),
                           O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    std::cerr << "another safe-ota process is active\n";
    return ExitCode::kOperationFailed;
  }

  std::string error;
  bool recovered = false;
  if (!upgrader::RecoverInterruptedUpgrade(options.install_root, &recovered, &error)) {
    std::cerr << error << '\n';
    return ExitCode::kOperationFailed;
  }
  if (recovered) {
    std::cout << "recovered interrupted upgrade\n";
  }
  if (options.recover_only) {
    return ExitCode::kSuccess;
  }
  if (!options.standalone) {
    const std::filesystem::path incoming_directory =
        std::filesystem::weakly_canonical(options.install_root / "data/ota/incoming");
    if (!std::filesystem::is_directory(incoming_directory) ||
        !IsWithinDirectory(options.package_root, incoming_directory)) {
      std::cerr << "online OTA package must be inside " << incoming_directory << '\n';
      return ExitCode::kInvalidArguments;
    }
  }

  std::string package_version;
  if (!upgrader::ReadUpgradePackageVersion(options.package_root, &package_version, &error)) {
    std::cerr << error << '\n';
    return ExitCode::kOperationFailed;
  }
  if (options.confirmed_version != package_version) {
    std::cerr << "confirmation version does not match package: " << package_version << '\n';
    return ExitCode::kInvalidArguments;
  }

  const upgrader::UpgradeRequest request{options.package_root, options.install_root,
                                         options.public_key, package_version};
  upgrader::UpgradeResult result;
  if (options.standalone) {
    if (!upgrader::InstallUpgrade(request, &result)) {
      std::string recovery_error;
      if (!upgrader::RecoverInterruptedUpgrade(options.install_root, nullptr, &recovery_error)) {
        std::cerr << result.error << "; " << recovery_error << '\n';
        return ExitCode::kOperationFailed;
      }
      std::cerr << result.error << '\n';
      return ExitCode::kOperationFailed;
    }
  } else {
    const std::filesystem::path request_path = options.install_root / "run/upgrade-request.yaml";
    const std::filesystem::path result_path = options.install_root / "run/upgrade-result.yaml";
    std::filesystem::remove(result_path, filesystem_error);
    if (!upgrader::SaveUpgradeRequest(request_path, request, &error)) {
      std::cerr << error << '\n';
      return ExitCode::kOperationFailed;
    }
    if (!SendRuntimeCommand(options.socket_path, "switch upgrade", &error)) {
      std::cerr << error << '\n';
      std::filesystem::remove(request_path, filesystem_error);
      return ExitCode::kOperationFailed;
    }
    if (!WaitForResult(result_path, kUpgradeResultTimeoutSeconds, &result, &error)) {
      std::string switch_error;
      SendRuntimeCommand(options.socket_path, "switch normal", &switch_error);
      std::string recovery_error;
      if (!upgrader::RecoverInterruptedUpgrade(options.install_root, nullptr, &recovery_error)) {
        error += "; " + recovery_error;
      }
      std::cerr << error << '\n';
      std::filesystem::remove(request_path, filesystem_error);
      return ExitCode::kOperationFailed;
    }
    std::filesystem::remove(request_path, filesystem_error);
    std::filesystem::remove(result_path, filesystem_error);
    if (result.state != upgrader::UpgradeState::kActivated) {
      SendRuntimeCommand(options.socket_path, "switch normal", &error);
      std::string recovery_error;
      if (!upgrader::RecoverInterruptedUpgrade(options.install_root, nullptr, &recovery_error)) {
        result.error += "; " + recovery_error;
      }
      std::cerr << result.error << '\n';
      return ExitCode::kOperationFailed;
    }
    if (!SendRuntimeCommand(options.socket_path, "reexec normal", &error)) {
      std::string rollback_error;
      if (!upgrader::RecoverInterruptedUpgrade(options.install_root, nullptr, &rollback_error)) {
        std::cerr << error << "; " << rollback_error << '\n';
        return ExitCode::kOperationFailed;
      }
      std::string restore_error;
      if (!SendRuntimeCommand(options.socket_path, "reexec normal", &restore_error)) {
        std::cerr << error
                  << "; rollback activated but normal mode restore failed: " << restore_error
                  << '\n';
        return ExitCode::kOperationFailed;
      }
      std::cerr << error << "; rolled back to " << result.previous_release << '\n';
      return ExitCode::kOperationFailed;
    }
  }

  const bool runtime_ready =
      options.standalone || WaitForRuntime(options.socket_path, options.install_root,
                                           package_version, options.timeout_seconds, &error);
  if (!runtime_ready ||
      !upgrader::WaitForUpgradeHealth(options.health_command, options.install_root,
                                      options.timeout_seconds, &error)) {
    std::string rollback_error;
    if (!upgrader::RecoverInterruptedUpgrade(options.install_root, nullptr, &rollback_error)) {
      std::cerr << error << "; " << rollback_error << '\n';
      return ExitCode::kOperationFailed;
    }
    if (!options.standalone) {
      std::string reload_error;
      if (!SendRuntimeCommand(options.socket_path, "reexec normal", &reload_error)) {
        std::cerr << error << "; rollback activated but runtime reexec failed: " << reload_error
                  << '\n';
        return ExitCode::kOperationFailed;
      }
      if (!WaitForRuntime(options.socket_path, options.install_root,
                          result.previous_release.filename().string(), options.timeout_seconds,
                          &reload_error)) {
        std::cerr << error << "; rollback Navigator did not become ready: " << reload_error << '\n';
        return ExitCode::kOperationFailed;
      }
    }
    std::cerr << error << "; rolled back to " << result.previous_release << '\n';
    return ExitCode::kUnhealthy;
  }

  if (!upgrader::ConfirmUpgrade(options.install_root, package_version, &error)) {
    std::string recovery_error;
    if (!upgrader::RecoverInterruptedUpgrade(options.install_root, nullptr, &recovery_error)) {
      error += "; " + recovery_error;
    }
    if (!options.standalone) {
      std::string reload_error;
      if (!SendRuntimeCommand(options.socket_path, "reexec normal", &reload_error)) {
        error += "; runtime reexec failed: " + reload_error;
      } else if (!WaitForRuntime(options.socket_path, options.install_root,
                                 result.previous_release.filename().string(),
                                 options.timeout_seconds, &reload_error)) {
        error += "; rollback Navigator did not become ready: " + reload_error;
      }
    }
    std::cerr << error << '\n';
    return ExitCode::kOperationFailed;
  }

  std::cout << "upgrade confirmed version=" << package_version << '\n';
  return ExitCode::kSuccess;
}

}  // namespace safe_ota
}  // namespace cockpit
