#include <iostream>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/runtime/args.h"
#include "tools/cockpit-ctl/dependencies_command.h"
#include "tools/cockpit-ctl/health_command.h"
#include "tools/cockpit-ctl/runtime_command.h"
#include "tools/cockpit-ctl/status_command.h"
#include "tools/diagnostics/cli_output.h"

namespace {

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  cockpit-ctl status [--config configs/config.yaml]\n"
            << "  cockpit-ctl status --watch [--interval SEC] [--config configs/config.yaml]\n"
            << "  cockpit-ctl health [--config configs/config.yaml]\n"
            << "  cockpit-ctl dependencies [--config configs/config.yaml]\n"
            << "  cockpit-ctl runtime status|mode|reload [--socket PATH]\n"
            << "  cockpit-ctl runtime switch MODE [--socket PATH]\n"
            << "  cockpit-ctl runtime start|stop|restart MODULE [--socket PATH]\n"
            << "\nOptions:\n"
            << "  --config PATH    config file path (default: configs/config.yaml)\n"
            << "  --socket PATH    Navigator Unix Socket path\n"
            << "  --watch          watch mode, refresh status periodically\n"
            << "  --output FORMAT  text or json (default: text)\n"
            << "  --interval SEC   refresh interval in seconds (default: "
            << cockpit::ctl::status::kDefaultWatchIntervalSec << ")\n";
}

}  // namespace

int main(int argc, char** argv) {
  const auto args = cockpit::runtime::Args::Parse(argc, argv);
  const std::string command = argc > 1 ? argv[1] : "";
  if (command.empty() || command == "help" || command == "--help" || command == "-h") {
    PrintUsage();
    return command.empty() ? 1 : 0;
  }
  if (command != "status" && command != "health" && command != "dependencies" &&
      command != "runtime") {
    std::cerr << "unknown command: " << command << '\n';
    PrintUsage();
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  if (command == "runtime") {
    return cockpit::ctl::runtime_command::Run(argc, argv, args);
  }

  const auto config =
      cockpit::config::SystemConfig::LoadFromFile(args.GetString("config", "configs/config.yaml"));
  cockpit::diagnostics::OutputFormat output_format;
  std::string output_error;
  if (!cockpit::diagnostics::ParseOutputFormat(args.GetString("output", "text"), &output_format,
                                               &output_error)) {
    std::cerr << output_error << '\n';
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }

  if (command == "health") {
    return cockpit::ctl::health::Run(config, output_format);
  }
  if (command == "dependencies") {
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      cockpit::diagnostics::WriteJsonError("invalid_arguments",
                                           "dependencies does not support JSON output", &std::cerr);
      return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
    }
    return cockpit::ctl::dependencies::Run(config);
  }

  const bool watch = args.HasFlag("watch");
  if (watch && output_format == cockpit::diagnostics::OutputFormat::kJson) {
    cockpit::diagnostics::WriteJsonError("invalid_arguments", "watch does not support JSON output",
                                         &std::cerr);
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  const int interval_sec = args.GetInt("interval", cockpit::ctl::status::kDefaultWatchIntervalSec);
  if (watch && interval_sec < 1) {
    std::cerr << "interval must be >= 1 second\n";
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  return cockpit::ctl::status::Run(config, output_format, watch, interval_sec);
}
