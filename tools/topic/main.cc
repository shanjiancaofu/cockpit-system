#include <iostream>
#include <string>

#include "topic_command_line.h"
#include "topic_echo.h"
#include "topic_hz.h"
#include "topic_info.h"
#include "topic_list.h"
#include "topic_pub.h"
#include "topic_usage.h"

#include "core/config/system_config.h"

int main(int argc, char** argv) {
  using namespace cockpit::topic;

  const auto line = ParseCommandLine(argc, argv);
  if (line.command.empty() || line.command == "help" || HasFlag(line, "help")) {
    PrintUsage();
    return line.command.empty() ? 2 : 0;
  }

  const std::string config_path = Option(line, "config", "configs/config.yaml");
  auto config = cockpit::config::SystemConfig::LoadFromFile(config_path);

  if (line.command == "list") {
    return RunListCommand(config, line);
  }
  if (line.command == "info") {
    return RunInfoCommand(config, line);
  }
  if (line.command == "pub") {
    return RunPubCommand(config, line);
  }
  if (line.command == "echo") {
    return RunEchoCommand(config, line);
  }
  if (line.command == "hz") {
    return RunHzCommand(config, line);
  }

  std::cerr << "unknown command: " << line.command << "\n";
  PrintUsage();
  return 2;
}
