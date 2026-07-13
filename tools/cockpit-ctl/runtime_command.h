#pragma once

#include "cockpit/core/runtime/args.h"

namespace cockpit {
namespace ctl {
namespace runtime_command {

int Run(int argc, char** argv, const runtime::Args& args);

}  // namespace runtime_command
}  // namespace ctl
}  // namespace cockpit
