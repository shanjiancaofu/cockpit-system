#pragma once

#include "common.pb.h"

namespace cockpit {
namespace health {

const char* StateName(proto::common::ServiceHealthState state);
int Severity(proto::common::ServiceHealthState state);
bool PassesHealthCheck(proto::common::ServiceHealthState state);

}  // namespace health
}  // namespace cockpit
