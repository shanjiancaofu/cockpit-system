#include <filesystem>
#include <iostream>
#include <string>

#include "cockpit/core/runtime/args.h"
#include "tools/diagnostics/cli_output.h"
#include "tools/safe-ota/action/safe_ota.h"
#include "tools/safe-ota/common/safe_ota_options.h"

namespace {

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  safe-ota --package PATH --confirm VERSION [--root PATH] [--socket PATH]\n"
            << "           [--health-command PATH] [--timeout SEC] [--standalone]\n"
            << "  safe-ota --recover [--root PATH]\n";
}

}  // namespace

int main(int argc, char** argv) {
  using cockpit::diagnostics::ExitCode;
  using cockpit::diagnostics::ToInt;

  const cockpit::runtime::Args args = cockpit::runtime::Args::Parse(argc, argv);
  if (args.HasFlag("help")) {
    PrintUsage();
    return ToInt(ExitCode::kSuccess);
  }

  const bool recover_only = args.HasFlag("recover");
  const std::string package_argument = args.GetString("package", "");
  const std::string confirmed_version = args.GetString("confirm", "");
  const int timeout_seconds = args.GetInt("timeout", 60);
  if (!recover_only &&
      (package_argument.empty() || confirmed_version.empty() || timeout_seconds <= 0)) {
    PrintUsage();
    return ToInt(ExitCode::kInvalidArguments);
  }

  try {
    cockpit::safe_ota::SafeOtaOptions options;
    options.install_root = std::filesystem::absolute(args.GetString("root", "/cockpit-system"));
    options.recover_only = recover_only;
    if (!recover_only) {
      options.package_root = std::filesystem::canonical(package_argument);
      options.confirmed_version = confirmed_version;
      options.health_command = std::filesystem::absolute(args.GetString(
          "health-command", (options.package_root / "deploy/healthcheck.sh").string()));
      options.socket_path =
          args.GetString("socket", (options.install_root / "run/navigator.sock").string());
      options.timeout_seconds = timeout_seconds;
      options.standalone = args.HasFlag("standalone");
    }
    return ToInt(cockpit::safe_ota::ExecuteSafeOta(options));
  } catch (const std::exception& exception) {
    std::cerr << "safe-ota: " << exception.what() << '\n';
    return ToInt(ExitCode::kOperationFailed);
  }
}
