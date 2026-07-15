#pragma once

#include <string>

#include "cockpit/core/config/system_config.h"
#include "tools/diagnostics/cli_output.h"

namespace cockpit {
namespace ctl {
namespace health {

int Run(const config::SystemConfig& config, diagnostics::OutputFormat output_format,
        const std::string& mode);

}  // namespace health
}  // namespace ctl
}  // namespace cockpit
