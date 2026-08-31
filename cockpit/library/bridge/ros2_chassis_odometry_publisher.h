#pragma once

#include <cstdint>
#include <memory>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>

#include "cockpit/modules/vehicle/chassis_odometry_time_mapper.h"
#include "cockpit/modules/vehicle/chassis_state.h"

namespace cockpit::bridge {

class Ros2ChassisOdometryPublisher final {
 public:
  Ros2ChassisOdometryPublisher(rclcpp::Node* node, std::string topic, std::string frame_id = "odom",
                               std::string child_frame_id = "base_link");

  bool Publish(const vehicle::ChassisState& state, std::int64_t received_realtime_ns,
               std::string* error = nullptr);
  std::uint64_t clock_reset_count() const {
    return clock_reset_count_;
  }
  void ResetDeviceClock() {
    time_mapper_.Reset();
  }

 private:
  std::string frame_id_;
  std::string child_frame_id_;
  vehicle::ChassisOdometryTimeMapper time_mapper_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr publisher_;
  std::uint64_t clock_reset_count_ = 0;
};

}  // namespace cockpit::bridge
