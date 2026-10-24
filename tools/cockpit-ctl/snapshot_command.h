#pragma once

#include "cockpit/exchange/foundation/config/system_config.h"
#include "cockpit/exchange/foundation/runtime/args.h"

namespace cockpit {
namespace ctl {
namespace snapshot {

int Run(const config::SystemConfig& config, const runtime::Args& args);

}  // namespace snapshot
}  // namespace ctl
}  // namespace cockpit
