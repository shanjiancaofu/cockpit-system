#pragma once

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/runtime/args.h"

namespace cockpit {
namespace ctl {
namespace snapshot {

int Run(const config::SystemConfig& config, const runtime::Args& args);

}  // namespace snapshot
}  // namespace ctl
}  // namespace cockpit
