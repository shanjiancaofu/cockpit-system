#include "tools/safe-ota/safe_ota.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/core/runtime/args.h"
#include "cockpit/library/upgrader/upgrade_transaction.h"
#include "cockpit/navigator/connection/ipc_connector.h"
#include "tools/diagnostics/cli_output.h"

namespace cockpit {
namespace safe_ota {
namespace {

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  safe-ota --package PATH --confirm VERSION [--root PATH] [--socket PATH]\n"
            << "           [--health-command PATH] [--timeout SEC] [--standalone]\n";
}

bool SendRuntimeCommand(const std::string& socket_path, const std::string& command,
                        std::string* error) {
  std::string response;
  if (!navigator::IpcConnector::SendRequest(socket_path, command, &response, error)) {
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

}  // namespace

int Run(int argc, char** argv) {
  using diagnostics::ExitCode;
  const runtime::Args args = runtime::Args::Parse(argc, argv);
  if (args.HasFlag("help")) {
    PrintUsage();
    return diagnostics::ToInt(ExitCode::kSuccess);
  }

  const std::string package_argument = args.GetString("package", "");
  const std::string confirmed_version = args.GetString("confirm", "");
  const int timeout_seconds = args.GetInt("timeout", 60);
  if (package_argument.empty() || confirmed_version.empty() || timeout_seconds <= 0) {
    PrintUsage();
    return diagnostics::ToInt(ExitCode::kInvalidArguments);
  }

  try {
    const std::filesystem::path package_root = std::filesystem::canonical(package_argument);
    const std::filesystem::path install_root =
        std::filesystem::absolute(args.GetString("root", "/cockpit-system"));
    const std::filesystem::path health_command = std::filesystem::absolute(
        args.GetString("health-command", (package_root / "deploy/healthcheck.sh").string()));
    const std::string socket_path =
        args.GetString("socket", (install_root / "run/navigator.sock").string());

    std::string package_version;
    std::string error;
    if (!upgrader::ReadUpgradePackageVersion(package_root, &package_version, &error)) {
      std::cerr << error << '\n';
      return diagnostics::ToInt(ExitCode::kOperationFailed);
    }
    if (confirmed_version != package_version) {
      std::cerr << "confirmation version does not match package: " << package_version << '\n';
      return diagnostics::ToInt(ExitCode::kInvalidArguments);
    }

    const upgrader::UpgradeRequest request{package_root, install_root, package_version};
    upgrader::UpgradeResult result;
    if (args.HasFlag("standalone")) {
      if (!upgrader::InstallUpgrade(request, &result)) {
        std::cerr << result.error << '\n';
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
    } else {
      const std::filesystem::path request_path = install_root / "run/upgrade-request.yaml";
      const std::filesystem::path result_path = install_root / "run/upgrade-result.yaml";
      std::error_code filesystem_error;
      std::filesystem::remove(result_path, filesystem_error);
      if (!upgrader::SaveUpgradeRequest(request_path, request, &error)) {
        std::cerr << error << '\n';
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
      if (!SendRuntimeCommand(socket_path, "switch upgrade", &error)) {
        std::cerr << error << '\n';
        std::filesystem::remove(request_path, filesystem_error);
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
      if (!WaitForResult(result_path, timeout_seconds, &result, &error)) {
        std::string switch_error;
        SendRuntimeCommand(socket_path, "switch normal", &switch_error);
        std::cerr << error << '\n';
        std::filesystem::remove(request_path, filesystem_error);
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
      std::filesystem::remove(request_path, filesystem_error);
      std::filesystem::remove(result_path, filesystem_error);
      if (result.state != upgrader::UpgradeState::kActivated) {
        SendRuntimeCommand(socket_path, "switch normal", &error);
        std::cerr << result.error << '\n';
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
      if (!SendRuntimeCommand(socket_path, "switch normal", &error)) {
        std::string rollback_error;
        if (!upgrader::RollbackUpgrade(install_root, result.previous_release, &rollback_error)) {
          std::cerr << error << "; " << rollback_error << '\n';
          return diagnostics::ToInt(ExitCode::kOperationFailed);
        }
        std::string restore_error;
        if (!SendRuntimeCommand(socket_path, "switch normal", &restore_error)) {
          std::cerr << error
                    << "; rollback activated but normal mode restore failed: " << restore_error
                    << '\n';
          return diagnostics::ToInt(ExitCode::kOperationFailed);
        }
        std::cerr << error << "; rolled back to " << result.previous_release << '\n';
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
    }

    if (!upgrader::WaitForUpgradeHealth(health_command, install_root, timeout_seconds, &error)) {
      std::string rollback_error;
      if (!upgrader::RollbackUpgrade(install_root, result.previous_release, &rollback_error)) {
        std::cerr << error << "; " << rollback_error << '\n';
        return diagnostics::ToInt(ExitCode::kOperationFailed);
      }
      if (!args.HasFlag("standalone")) {
        std::string reload_error;
        if (!SendRuntimeCommand(socket_path, "reload", &reload_error)) {
          std::cerr << error << "; rollback activated but runtime reload failed: " << reload_error
                    << '\n';
          return diagnostics::ToInt(ExitCode::kOperationFailed);
        }
      }
      std::cerr << error << "; rolled back to " << result.previous_release << '\n';
      return diagnostics::ToInt(ExitCode::kUnhealthy);
    }

    std::cout << "upgrade confirmed version=" << package_version << '\n';
    return diagnostics::ToInt(ExitCode::kSuccess);
  } catch (const std::exception& exception) {
    std::cerr << "safe-ota: " << exception.what() << '\n';
    return diagnostics::ToInt(ExitCode::kOperationFailed);
  }
}

}  // namespace safe_ota
}  // namespace cockpit
