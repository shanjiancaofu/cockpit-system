#pragma once

#include <string>

#include "cockpit/exchange/foundation/config/system_config.h"
#include "tools/diagnostics/cli_output.h"

namespace cockpit {
namespace ctl {
namespace status {

constexpr int kDefaultWatchIntervalSec = 2;

std::string CaptureJson(const config::SystemConfig& config);
int Run(const config::SystemConfig& config, diagnostics::OutputFormat output_format, bool watch,
        int interval_sec);

}  // namespace status
}  // namespace ctl
}  // namespace cockpit
