#include "cockpit/modules/vehicle/chassis_client.h"

#include <algorithm>

#include "cockpit/core/time/time.h"

namespace cockpit::vehicle {

ChassisClient::ChassisClient() : started_ms_(time::NowMs()), heartbeat_due_ms_(started_ms_) {
  state_.timestamp_ms = started_ms_;
}

ChassisClientDecodeStatus ChassisClient::ProcessFrame(const can::CanFrame& frame,
                                                      std::int64_t now_ms, ChassisState* state) {
  if (now_ms < 0 || state == nullptr) {
    return ChassisClientDecodeStatus::kInvalid;
  }
  ChassisMotionStatus motion;
  if (frame.id() == ChassisCanCodec::kMotionStatusId) {
    if (!ChassisCanCodec::DecodeMotion(frame, &motion)) {
      return ChassisClientDecodeStatus::kInvalid;
    }
    state_.motion_valid = motion.valid;
    state_.running = motion.running;
    state_.control_state = motion.control_state;
    state_.left_velocity_mm_s = motion.left_velocity_mm_s;
    state_.right_velocity_mm_s = motion.right_velocity_mm_s;
    state_.linear_velocity_mm_s = motion.linear_velocity_mm_s;
    state_.angular_velocity_mrad_s = motion.angular_velocity_mrad_s;
    state_.left_output_permille = motion.left_output_permille;
    state_.right_output_permille = motion.right_output_permille;
  } else if (frame.id() == ChassisCanCodec::kOdometryReportId) {
    ChassisOdometryReport odometry;
    if (!ChassisCanCodec::DecodeOdometry(frame, &odometry)) {
      return ChassisClientDecodeStatus::kInvalid;
    }
    state_.odometry_valid = odometry.valid;
    state_.odometry_timestamp_ms = odometry.timestamp_ms;
    state_.x_mm = odometry.x_mm;
    state_.y_mm = odometry.y_mm;
    state_.heading_mrad = odometry.heading_mrad;
  } else if (frame.id() == ChassisCanCodec::kHeartbeatId) {
    if (!heartbeat_monitor_.Process(frame, now_ms)) {
      ChassisHeartbeat decoded;
      return ChassisCanCodec::DecodeHeartbeat(frame, &decoded)
                 ? ChassisClientDecodeStatus::kIgnored
                 : ChassisClientDecodeStatus::kInvalid;
    }
    RefreshHeartbeat(now_ms);
    reported_heartbeat_status_ = state_.heartbeat_status;
  } else if (frame.id() == ChassisCanCodec::kFaultStatusId) {
    ChassisFaultStatus fault;
    if (!ChassisCanCodec::DecodeFault(frame, &fault)) {
      return ChassisClientDecodeStatus::kInvalid;
    }
    state_.fault_severity = fault.severity;
    state_.active_faults = fault.active_faults;
    state_.latched_faults = fault.latched_faults;
    state_.fault_sequence = fault.fault_sequence;
  } else {
    return ChassisClientDecodeStatus::kIgnored;
  }
  state_.timestamp_ms = now_ms;
  *state = state_;
  return ChassisClientDecodeStatus::kUpdated;
}

bool ChassisClient::Update(std::int64_t now_ms, ChassisState* state) {
  if (now_ms < 0 || state == nullptr) {
    return false;
  }
  heartbeat_monitor_.Update(now_ms);
  RefreshHeartbeat(now_ms);
  if (state_.heartbeat_status == reported_heartbeat_status_) {
    return false;
  }
  reported_heartbeat_status_ = state_.heartbeat_status;
  state_.timestamp_ms = now_ms;
  *state = state_;
  return true;
}

bool ChassisClient::HeartbeatDue(std::int64_t now_ms) const {
  return now_ms >= heartbeat_due_ms_;
}

bool ChassisClient::BuildHeartbeat(std::int64_t now_ms, can::CanFrame* frame) {
  if (frame == nullptr || now_ms < started_ms_ || !HeartbeatDue(now_ms)) {
    return false;
  }
  const auto uptime = static_cast<std::uint64_t>(now_ms - started_ms_);
  const ChassisHeartbeat heartbeat{
      1U, heartbeat_sequence_, 1U,
      static_cast<std::uint32_t>(std::min<std::uint64_t>(uptime, UINT32_MAX)), 0U};
  if (!ChassisCanCodec::EncodeHeartbeat(heartbeat, frame)) {
    return false;
  }
  ++heartbeat_sequence_;
  heartbeat_due_ms_ = now_ms + kHeartbeatPeriodMs;
  return true;
}

ChassisState ChassisClient::GetState(std::int64_t now_ms) const {
  ChassisState result = state_;
  const auto heartbeat = heartbeat_monitor_.GetSnapshot(now_ms);
  result.heartbeat_status = heartbeat.status;
  result.heartbeat_age_ms = heartbeat.age_ms;
  return result;
}

void ChassisClient::RefreshHeartbeat(std::int64_t now_ms) {
  const auto heartbeat = heartbeat_monitor_.GetSnapshot(now_ms);
  state_.heartbeat_status = heartbeat.status;
  state_.heartbeat_age_ms = heartbeat.age_ms;
  state_.node_state = heartbeat.heartbeat.node_state;
  state_.uptime_ms = heartbeat.heartbeat.uptime_ms;
  state_.fault_summary = heartbeat.heartbeat.fault_summary;
}

}  // namespace cockpit::vehicle
