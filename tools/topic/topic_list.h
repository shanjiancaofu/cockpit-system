#pragma once

#include "topic_command_line.h"

namespace cockpit {
namespace config {
class Config;
}  // namespace config
namespace topic {

int RunListCommand(const cockpit::config::Config& config, const CommandLine& line);

}  // namespace topic
}  // namespace cockpit
