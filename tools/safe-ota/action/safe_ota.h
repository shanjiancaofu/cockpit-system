#pragma once

#include "tools/diagnostics/cli_output.h"
#include "tools/safe-ota/common/safe_ota_options.h"

namespace cockpit {
namespace safe_ota {

diagnostics::ExitCode ExecuteSafeOta(const SafeOtaOptions& options);

}  // namespace safe_ota
}  // namespace cockpit
