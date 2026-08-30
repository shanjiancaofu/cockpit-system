#pragma once

#include <cstdint>

#include "cockpit/modules/can/can_frame.h"
#include "cockpit/modules/vehicle/chassis_client.h"
#include "cockpit/modules/vehicle/chassis_safety_adapter.h"

namespace cockpit::vehicle {

class ChassisCanSafetyStateSource final {
 public:
  explicit ChassisCanSafetyStateSource(std::int64_t started_steady_ms = 0);

  ChassisClientDecodeStatus ProcessFrame(const can::CanFrame& frame, std::int64_t steady_now_ms);
  ChassisSafetyState Evaluate(const ChassisSafetyState& controls, std::int64_t steady_now_ms);
  const ChassisState& chassis_state() const {
    return chassis_state_;
  }

 private:
  ChassisClient client_;
  ChassisState chassis_state_;
  ChassisStateFreshnessPolicy freshness_policy_;
  ChassisSafetyStateTracker tracker_;
  bool fault_seen_ = false;
};

}  // namespace cockpit::vehicle
