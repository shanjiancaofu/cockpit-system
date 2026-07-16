#include "cockpit/core/health/service_health.h"

namespace cockpit {
namespace health {

const char* StateName(proto::common::ServiceHealthState state) {
  switch (state) {
    case proto::common::SERVICE_HEALTH_STATE_OK:
      return "ok";
    case proto::common::SERVICE_HEALTH_STATE_DISABLED:
      return "disabled";
    case proto::common::SERVICE_HEALTH_STATE_DEGRADED:
      return "degraded";
    case proto::common::SERVICE_HEALTH_STATE_UNKNOWN:
    case proto::common::SERVICE_HEALTH_STATE_UNSPECIFIED:
      return "unknown";
    case proto::common::SERVICE_HEALTH_STATE_FAULTED:
      return "faulted";
    default:
      return "unknown";
  }
}

int Severity(proto::common::ServiceHealthState state) {
  switch (state) {
    case proto::common::SERVICE_HEALTH_STATE_OK:
      return 0;
    case proto::common::SERVICE_HEALTH_STATE_DISABLED:
      return 1;
    case proto::common::SERVICE_HEALTH_STATE_DEGRADED:
      return 2;
    case proto::common::SERVICE_HEALTH_STATE_UNKNOWN:
    case proto::common::SERVICE_HEALTH_STATE_UNSPECIFIED:
      return 3;
    case proto::common::SERVICE_HEALTH_STATE_FAULTED:
      return 4;
    default:
      return 3;
  }
}

bool PassesHealthCheck(proto::common::ServiceHealthState state) {
  return state == proto::common::SERVICE_HEALTH_STATE_OK ||
         state == proto::common::SERVICE_HEALTH_STATE_DISABLED ||
         state == proto::common::SERVICE_HEALTH_STATE_DEGRADED;
}

}  // namespace health
}  // namespace cockpit
