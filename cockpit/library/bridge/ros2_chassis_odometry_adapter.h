#pragma once

#include <builtin_interfaces/msg/time.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <string>

#include "cockpit/modules/vehicle/chassis_state.h"

namespace cockpit::bridge {

// Converts an already time-mapped 0x181 state into ROS odometry. The caller owns
// conversion from the STM32 millisecond clock into the ROS/realtime domain.
nav_msgs::msg::Odometry ToRosChassisOdometry(const vehicle::ChassisState& state,
                                             const std::string& frame_id,
                                             const std::string& child_frame_id,
                                             const builtin_interfaces::msg::Time& sample_stamp);

}  // namespace cockpit::bridge
