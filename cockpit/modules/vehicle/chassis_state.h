#pragma once

#include <cstdint>
#include <string>

#include "cockpit/modules/vehicle/chassis_can_codec.h"

namespace cockpit::vehicle {

struct ChassisState {
  std::int64_t timestamp_ms = 0;
  std::string source = "socketcan";

  bool motion_valid = false;
  bool running = false;
  std::uint8_t control_state = 0;
  std::int16_t left_velocity_mm_s = 0;
  std::int16_t right_velocity_mm_s = 0;
  std::int16_t linear_velocity_mm_s = 0;
  std::int16_t angular_velocity_mrad_s = 0;
  std::int16_t left_output_permille = 0;
  std::int16_t right_output_permille = 0;

  bool odometry_valid = false;
  std::uint32_t odometry_timestamp_ms = 0;
  std::int32_t x_mm = 0;
  std::int32_t y_mm = 0;
  std::int32_t heading_mrad = 0;
  std::int16_t odometry_linear_velocity_mm_s = 0;
  std::int16_t odometry_angular_velocity_mrad_s = 0;

  ChassisHeartbeatStatus heartbeat_status = ChassisHeartbeatStatus::kUnknown;
  std::int64_t heartbeat_age_ms = 0;
  std::uint8_t node_state = 0;
  std::uint32_t uptime_ms = 0;
  std::uint16_t fault_summary = 0;

  std::uint8_t fault_severity = 0;
  std::uint32_t active_faults = 0;
  std::uint32_t latched_faults = 0;
  std::uint16_t fault_sequence = 0;

  std::string ToJson() const;
};

}  // namespace cockpit::vehicle
