#include "cockpit/library/bridge/ros2_chassis_odometry_adapter.h"

#include <cmath>
#include <stdexcept>

namespace cockpit::bridge {

nav_msgs::msg::Odometry ToRosChassisOdometry(const vehicle::ChassisState& state,
                                             const std::string& frame_id,
                                             const std::string& child_frame_id,
                                             const builtin_interfaces::msg::Time& sample_stamp) {
  if (!state.odometry_valid || frame_id.empty() || child_frame_id.empty() || sample_stamp.sec < 0 ||
      sample_stamp.nanosec >= 1000000000U) {
    throw std::invalid_argument("invalid chassis odometry conversion input");
  }

  nav_msgs::msg::Odometry result;
  result.header.stamp = sample_stamp;
  result.header.frame_id = frame_id;
  result.child_frame_id = child_frame_id;
  result.pose.pose.position.x = static_cast<double>(state.x_mm) / 1000.0;
  result.pose.pose.position.y = static_cast<double>(state.y_mm) / 1000.0;
  const double yaw_rad = static_cast<double>(state.heading_mrad) / 1000.0;
  result.pose.pose.orientation.z = std::sin(yaw_rad / 2.0);
  result.pose.pose.orientation.w = std::cos(yaw_rad / 2.0);
  result.twist.twist.linear.x = static_cast<double>(state.odometry_linear_velocity_mm_s) / 1000.0;
  result.twist.twist.angular.z =
      static_cast<double>(state.odometry_angular_velocity_mrad_s) / 1000.0;
  return result;
}

}  // namespace cockpit::bridge
