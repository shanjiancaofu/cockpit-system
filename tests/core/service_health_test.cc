#include "cockpit/core/health/service_health.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using cockpit::proto::common::SERVICE_HEALTH_STATE_DEGRADED;
  using cockpit::proto::common::SERVICE_HEALTH_STATE_DISABLED;
  using cockpit::proto::common::SERVICE_HEALTH_STATE_FAULTED;
  using cockpit::proto::common::SERVICE_HEALTH_STATE_OK;
  using cockpit::proto::common::SERVICE_HEALTH_STATE_UNKNOWN;
  using cockpit::proto::common::SERVICE_HEALTH_STATE_UNSPECIFIED;

  return Check(
             std::string(cockpit::health::StateName(SERVICE_HEALTH_STATE_UNSPECIFIED)) == "unknown",
             "unspecified health state was not normalized") &&
                 Check(cockpit::health::Severity(SERVICE_HEALTH_STATE_OK) <
                           cockpit::health::Severity(SERVICE_HEALTH_STATE_DISABLED),
                       "disabled health severity mismatch") &&
                 Check(cockpit::health::Severity(SERVICE_HEALTH_STATE_DISABLED) <
                           cockpit::health::Severity(SERVICE_HEALTH_STATE_DEGRADED),
                       "degraded health severity mismatch") &&
                 Check(cockpit::health::Severity(SERVICE_HEALTH_STATE_DEGRADED) <
                           cockpit::health::Severity(SERVICE_HEALTH_STATE_UNKNOWN),
                       "unknown health severity mismatch") &&
                 Check(cockpit::health::Severity(SERVICE_HEALTH_STATE_UNKNOWN) <
                           cockpit::health::Severity(SERVICE_HEALTH_STATE_FAULTED),
                       "faulted health severity mismatch") &&
                 Check(cockpit::health::PassesHealthCheck(SERVICE_HEALTH_STATE_OK),
                       "ok state failed health check") &&
                 Check(cockpit::health::PassesHealthCheck(SERVICE_HEALTH_STATE_DISABLED),
                       "disabled state failed health check") &&
                 Check(cockpit::health::PassesHealthCheck(SERVICE_HEALTH_STATE_DEGRADED),
                       "degraded state failed health check") &&
                 Check(!cockpit::health::PassesHealthCheck(SERVICE_HEALTH_STATE_UNKNOWN),
                       "unknown state passed health check") &&
                 Check(!cockpit::health::PassesHealthCheck(SERVICE_HEALTH_STATE_FAULTED),
                       "faulted state passed health check")
             ? 0
             : 1;
}
