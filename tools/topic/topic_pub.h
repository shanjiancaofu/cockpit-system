#pragma once

#include "topic_command_line.h"

namespace cockpit {
namespace config {
class SystemConfig;
}  // namespace config
namespace topic {

int RunPubCommand(const cockpit::config::SystemConfig& config, const CommandLine& line);

}  // namespace topic
}  // namespace cockpit
