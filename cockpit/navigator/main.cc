#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/args.h"
#include "cockpit/navigator/connection/ipc_connector.h"
#include "cockpit/navigator/navigator.h"
#include "cockpit/navigator/run_config/run_config.h"

namespace {

std::string ExecutablePath() {
  char path[4096];
  const ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (size <= 0) {
    throw std::runtime_error("failed to resolve /proc/self/exe");
  }
  path[size] = '\0';
  return path;
}

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  cockpit-navigator [--config PATH] [--module-dir PATH] [--mode NAME]\n"
            << "  cockpit-navigator --command COMMAND [--socket PATH]\n";
}

}  // namespace

int main(int argc, char** argv) {
  using cockpit::navigator::IpcConnector;
  using cockpit::navigator::RunConfig;
  using cockpit::runtime::Args;

  try {
    const Args args = Args::Parse(argc, argv);
    if (args.HasFlag("help")) {
      PrintUsage();
      return 0;
    }

    if (args.HasFlag("module-child")) {
      const std::string name = args.GetString("module", "");
      const std::string library = args.GetString("library", "");
      if (name.empty() || library.empty()) {
        std::cerr << "module child requires --module and --library\n";
        return 64;
      }
      return cockpit::navigator::RunModuleChild(name, library, args.GetString("module-config", ""),
                                                args.GetInt("ready-fd", -1));
    }

    RunConfig config = RunConfig::Default();
    const std::string socket_path = args.GetString("socket", config.socket_path);
    const std::string command = args.GetString("command", "");
    if (!command.empty()) {
      std::string response;
      std::string error;
      if (!IpcConnector::SendRequest(socket_path, command, &response, &error)) {
        std::cerr << error << '\n';
        return 1;
      }
      std::cout << response;
      return response.rfind("OK", 0) == 0 ? 0 : 1;
    }

    const std::string config_path = args.GetString("config", "configs/config.yaml");
    const auto system_config = cockpit::config::SystemConfig::LoadFromFile(config_path);
    cockpit::logging::InitLogger(
        "cockpit-navigator", system_config.paths().log_dir,
        cockpit::logging::ParseLevel(system_config.logging().level),
        system_config.logging().mirror_stderr, system_config.logging().dump_time_secs,
        system_config.logging().cut_off_time_mins, system_config.logging().max_files);
    config.socket_path = socket_path;
    const std::string mode = args.GetString("mode", "");
    if (!mode.empty()) {
      config.initial_mode = mode;
      if (config.modes.find(mode) == config.modes.end()) {
        throw std::runtime_error("unknown mode: " + mode);
      }
    }
    const std::filesystem::path executable = ExecutablePath();
    const std::string default_module_dir =
        (executable.parent_path().parent_path() / "lib/cockpit/modules").string();
    const std::string crash_report_directory =
        (std::filesystem::path(system_config.paths().data_dir) / "crashes").string();
    cockpit::navigator::Navigator navigator(std::move(config), executable.string(),
                                            args.GetString("module-dir", default_module_dir),
                                            config_path, crash_report_directory);
    return navigator.Run();
  } catch (const std::exception& error) {
    std::cerr << "cockpit-navigator: " << error.what() << '\n';
    return 1;
  }
}
