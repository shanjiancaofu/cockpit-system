#include "cockpit/modules/vehicle/chassis_safety_adapter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cockpit::vehicle {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

std::int32_t Approach(std::int32_t current, std::int32_t target, std::int64_t maximum_delta) {
  const auto delta = static_cast<std::int64_t>(target) - current;
  const auto bounded = std::clamp(delta, -maximum_delta, maximum_delta);
  return static_cast<std::int32_t>(static_cast<std::int64_t>(current) + bounded);
}

bool ToScaledInteger(double value, double scale, std::int32_t* output) {
  if (!std::isfinite(value)) return false;
  const double scaled = value * scale;
  if (scaled < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return false;
  }
  *output = static_cast<std::int32_t>(std::llround(scaled));
  return true;
}

}  // namespace

bool ChassisSafetyPolicy::IsValid() const {
  return max_linear_velocity_mm_s > 0 && max_linear_velocity_mm_s <= 2000 &&
         max_angular_velocity_mrad_s > 0 && max_angular_velocity_mrad_s <= 10000 &&
         max_linear_acceleration_mm_s2 > 0 && max_angular_acceleration_mrad_s2 > 0 &&
         command_timeout_ms > 0 && output_period_ms > 0 && output_period_ms <= command_timeout_ms;
}

ChassisSafetyAdapter::ChassisSafetyAdapter(ChassisSafetyPolicy policy) : policy_(policy) {
}

bool ChassisSafetyAdapter::Submit(const ChassisVelocityRequest& request,
                                  const ChassisSafetyState& state, std::int64_t steady_now_ms,
                                  std::string* error) {
  if (!policy_.IsValid()) {
    invalid_command_pending_ = true;
    request_.reset();
    AssignError(error, "invalid chassis safety policy");
    return false;
  }
  if (!state.enabled || !state.authority_granted || state.emergency_stop || !state.peer_alive ||
      state.chassis_fault) {
    request_.reset();
    last_request_ms_ = 0;
    AssignError(error, "velocity command rejected while safety interlock is active");
    return false;
  }
  std::int32_t unused_linear = 0;
  std::int32_t unused_angular = 0;
  if (steady_now_ms < 0 || !ToScaledInteger(request.linear_velocity_m_s, 1000.0, &unused_linear) ||
      !ToScaledInteger(request.angular_velocity_rad_s, 1000.0, &unused_angular)) {
    invalid_command_pending_ = true;
    request_.reset();
    last_request_ms_ = steady_now_ms;
    AssignError(error, "velocity command must be finite and representable");
    return false;
  }
  request_ = request;
  last_request_ms_ = steady_now_ms;
  invalid_command_pending_ = false;
  return true;
}

SafeChassisCommand ChassisSafetyAdapter::Evaluate(const ChassisSafetyState& state,
                                                  std::int64_t steady_now_ms) {
  if (!policy_.IsValid()) return Stop(ChassisStopReason::kInvalidCommand, steady_now_ms);
  if (state.emergency_stop) return Stop(ChassisStopReason::kEmergencyStop, steady_now_ms);
  if (state.chassis_fault) return Stop(ChassisStopReason::kChassisFault, steady_now_ms);
  if (!state.peer_alive) return Stop(ChassisStopReason::kPeerUnavailable, steady_now_ms);
  if (!state.authority_granted) return Stop(ChassisStopReason::kAuthorityLost, steady_now_ms);
  if (!state.enabled) return Stop(ChassisStopReason::kDisabled, steady_now_ms);
  if (invalid_command_pending_) return Stop(ChassisStopReason::kInvalidCommand, steady_now_ms);
  if (!request_.has_value()) return Stop(ChassisStopReason::kNoCommand, steady_now_ms);
  if (steady_now_ms < last_request_ms_ || steady_now_ms < last_output_ms_) {
    return Stop(ChassisStopReason::kClockRegression, steady_now_ms);
  }
  if (steady_now_ms - last_request_ms_ > policy_.command_timeout_ms) {
    return Stop(ChassisStopReason::kCommandStale, steady_now_ms);
  }

  std::int32_t target_linear = 0;
  std::int32_t target_angular = 0;
  if (!ToScaledInteger(request_->linear_velocity_m_s, 1000.0, &target_linear) ||
      !ToScaledInteger(request_->angular_velocity_rad_s, 1000.0, &target_angular)) {
    return Stop(ChassisStopReason::kInvalidCommand, steady_now_ms);
  }
  target_linear = std::clamp(target_linear, -policy_.max_linear_velocity_mm_s,
                             policy_.max_linear_velocity_mm_s);
  target_angular = std::clamp(target_angular, -policy_.max_angular_velocity_mrad_s,
                              policy_.max_angular_velocity_mrad_s);

  const std::int64_t elapsed_ms = last_output_ms_ == 0
                                      ? policy_.output_period_ms
                                      : std::clamp(steady_now_ms - last_output_ms_, std::int64_t{0},
                                                   policy_.command_timeout_ms);
  const std::int64_t linear_delta = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(policy_.max_linear_acceleration_mm_s2) * elapsed_ms / 1000);
  const std::int64_t angular_delta = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(policy_.max_angular_acceleration_mrad_s2) * elapsed_ms / 1000);
  last_linear_mm_s_ = Approach(last_linear_mm_s_, target_linear, linear_delta);
  last_angular_mrad_s_ = Approach(last_angular_mrad_s_, target_angular, angular_delta);
  last_output_ms_ = steady_now_ms;

  SafeChassisCommand command;
  command.enabled = true;
  command.sequence = next_sequence_++;
  command.linear_velocity_mm_s = static_cast<std::int16_t>(last_linear_mm_s_);
  command.angular_velocity_mrad_s = static_cast<std::int16_t>(last_angular_mrad_s_);
  command.generated_at_steady_ms = steady_now_ms;
  command.stop_reason = ChassisStopReason::kNone;
  return command;
}

void ChassisSafetyAdapter::Reset(std::int64_t steady_now_ms) {
  request_.reset();
  invalid_command_pending_ = false;
  last_request_ms_ = 0;
  last_output_ms_ = steady_now_ms;
  last_linear_mm_s_ = 0;
  last_angular_mrad_s_ = 0;
}

SafeChassisCommand ChassisSafetyAdapter::Stop(ChassisStopReason reason,
                                              std::int64_t steady_now_ms) {
  request_.reset();
  last_request_ms_ = 0;
  last_linear_mm_s_ = 0;
  last_angular_mrad_s_ = 0;
  last_output_ms_ = std::max<std::int64_t>(0, steady_now_ms);
  SafeChassisCommand command;
  command.sequence = next_sequence_++;
  command.generated_at_steady_ms = steady_now_ms;
  command.stop_reason = reason;
  return command;
}

const char* ToString(ChassisStopReason reason) {
  switch (reason) {
    case ChassisStopReason::kNone:
      return "none";
    case ChassisStopReason::kDisabled:
      return "disabled";
    case ChassisStopReason::kAuthorityLost:
      return "authority_lost";
    case ChassisStopReason::kEmergencyStop:
      return "emergency_stop";
    case ChassisStopReason::kPeerUnavailable:
      return "peer_unavailable";
    case ChassisStopReason::kChassisFault:
      return "chassis_fault";
    case ChassisStopReason::kNoCommand:
      return "no_command";
    case ChassisStopReason::kInvalidCommand:
      return "invalid_command";
    case ChassisStopReason::kCommandStale:
      return "command_stale";
    case ChassisStopReason::kClockRegression:
      return "clock_regression";
  }
  return "unknown";
}

}  // namespace cockpit::vehicle
