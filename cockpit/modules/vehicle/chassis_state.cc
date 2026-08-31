#include "cockpit/modules/vehicle/chassis_state.h"

#include <sstream>

#include "cockpit/core/json/json.h"

namespace cockpit::vehicle {
namespace {

const char* HeartbeatStatusText(ChassisHeartbeatStatus status) {
  switch (status) {
    case ChassisHeartbeatStatus::kAlive:
      return "alive";
    case ChassisHeartbeatStatus::kTimeout:
      return "timeout";
    case ChassisHeartbeatStatus::kUnknown:
    default:
      return "unknown";
  }
}

}  // namespace

std::string ChassisState::ToJson() const {
  std::ostringstream out;
  out << '{' << "\"timestamp_ms\":" << timestamp_ms << ',' << "\"source\":\""
      << json::EscapeString(source) << "\","
      << "\"motion_valid\":" << (motion_valid ? "true" : "false") << ','
      << "\"running\":" << (running ? "true" : "false") << ','
      << "\"control_state\":" << static_cast<unsigned int>(control_state) << ','
      << "\"left_velocity_mm_s\":" << left_velocity_mm_s << ','
      << "\"right_velocity_mm_s\":" << right_velocity_mm_s << ','
      << "\"linear_velocity_mm_s\":" << linear_velocity_mm_s << ','
      << "\"angular_velocity_mrad_s\":" << angular_velocity_mrad_s << ','
      << "\"left_output_permille\":" << left_output_permille << ','
      << "\"right_output_permille\":" << right_output_permille << ','
      << "\"odometry_valid\":" << (odometry_valid ? "true" : "false") << ','
      << "\"odometry_timestamp_ms\":" << odometry_timestamp_ms << ',' << "\"x_mm\":" << x_mm << ','
      << "\"y_mm\":" << y_mm << ',' << "\"heading_mrad\":" << heading_mrad << ','
      << "\"odometry_linear_velocity_mm_s\":" << odometry_linear_velocity_mm_s << ','
      << "\"odometry_angular_velocity_mrad_s\":" << odometry_angular_velocity_mrad_s << ','
      << "\"heartbeat_status\":\"" << HeartbeatStatusText(heartbeat_status) << "\","
      << "\"heartbeat_age_ms\":" << heartbeat_age_ms << ','
      << "\"node_state\":" << static_cast<unsigned int>(node_state) << ','
      << "\"uptime_ms\":" << uptime_ms << ',' << "\"peer_reboot_count\":" << peer_reboot_count
      << ',' << "\"fault_summary\":" << fault_summary << ','
      << "\"fault_severity\":" << static_cast<unsigned int>(fault_severity) << ','
      << "\"active_faults\":" << active_faults << ',' << "\"latched_faults\":" << latched_faults
      << ',' << "\"fault_sequence\":" << fault_sequence << '}';
  return out.str();
}

}  // namespace cockpit::vehicle
