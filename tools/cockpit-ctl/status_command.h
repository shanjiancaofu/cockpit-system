#pragma once

#include "cockpit/core/config/system_config.h"
#include "tools/diagnostics/cli_output.h"

namespace cockpit {
namespace ctl {
namespace status {

constexpr int kDefaultWatchIntervalSec = 2;

int Run(const config::SystemConfig& config, diagnostics::OutputFormat output_format, bool watch,
        int interval_sec);

}  // namespace status
}  // namespace ctl
}  // namespace cockpit
