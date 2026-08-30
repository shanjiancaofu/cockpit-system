#include "cockpit/modules/vehicle/chassis_can_safety_state_source.h"

namespace cockpit::vehicle {

ChassisCanSafetyStateSource::ChassisCanSafetyStateSource(std::int64_t started_steady_ms)
    : client_(started_steady_ms), tracker_(freshness_policy_) {
}

ChassisClientDecodeStatus ChassisCanSafetyStateSource::ProcessFrame(const can::CanFrame& frame,
                                                                    std::int64_t steady_now_ms) {
  const auto result = client_.ProcessFrame(frame, steady_now_ms, &chassis_state_);
  if (result == ChassisClientDecodeStatus::kUpdated &&
      frame.id() == ChassisCanCodec::kHeartbeatId) {
    tracker_.UpdatePeerHeartbeat(steady_now_ms);
  }
  if (result == ChassisClientDecodeStatus::kUpdated &&
      frame.id() == ChassisCanCodec::kFaultStatusId) {
    fault_seen_ = true;
    tracker_.UpdateChassisFault(
        chassis_state_.fault_severity != 0U || chassis_state_.active_faults != 0U, steady_now_ms);
  }
  return result;
}

ChassisSafetyState ChassisCanSafetyStateSource::Evaluate(const ChassisSafetyState& controls,
                                                         std::int64_t steady_now_ms) {
  auto result = tracker_.Evaluate(controls, steady_now_ms);
  if (!fault_seen_) result.chassis_fault = true;
  return result;
}

}  // namespace cockpit::vehicle
