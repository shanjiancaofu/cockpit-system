#include "cockpit/library/bridge/ros2_chassis_odometry_publisher.h"

#include <limits>
#include <stdexcept>
#include <utility>

#include "cockpit/library/bridge/ros2_chassis_odometry_adapter.h"

namespace cockpit::bridge {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

bool ToRosStamp(std::int64_t timestamp_ns, builtin_interfaces::msg::Time* stamp) {
  if (stamp == nullptr || timestamp_ns <= 0) return false;
  const std::int64_t seconds = timestamp_ns / 1000000000LL;
  if (seconds > std::numeric_limits<std::int32_t>::max()) return false;
  stamp->sec = static_cast<std::int32_t>(seconds);
  stamp->nanosec = static_cast<std::uint32_t>(timestamp_ns % 1000000000LL);
  return true;
}

}  // namespace

Ros2ChassisOdometryPublisher::Ros2ChassisOdometryPublisher(rclcpp::Node* node, std::string topic,
                                                           std::string frame_id,
                                                           std::string child_frame_id)
    : frame_id_(std::move(frame_id)), child_frame_id_(std::move(child_frame_id)) {
  if (node == nullptr || topic.empty() || frame_id_.empty() || child_frame_id_.empty()) {
    throw std::invalid_argument("invalid ROS2 chassis odometry publisher configuration");
  }
  publisher_ = node->create_publisher<nav_msgs::msg::Odometry>(std::move(topic), 10);
}

bool Ros2ChassisOdometryPublisher::Publish(const vehicle::ChassisState& state,
                                           std::int64_t received_realtime_ns, std::string* error) {
  if (!state.odometry_valid) {
    AssignError(error, "cannot publish invalid chassis odometry");
    return false;
  }
  if (!peer_reboot_count_valid_) {
    peer_reboot_count_valid_ = true;
    last_peer_reboot_count_ = state.peer_reboot_count;
  } else if (state.peer_reboot_count != last_peer_reboot_count_) {
    time_mapper_.Reset();
    last_peer_reboot_count_ = state.peer_reboot_count;
  }
  std::int64_t sample_realtime_ns = 0;
  const auto map_status =
      time_mapper_.Map(state.odometry_timestamp_ms, received_realtime_ns, &sample_realtime_ns);
  if (map_status == vehicle::ChassisOdometryTimeMapStatus::kInvalid) {
    AssignError(error, "failed to map STM32 odometry time into ROS time domain");
    return false;
  }
  if (map_status == vehicle::ChassisOdometryTimeMapStatus::kReset) ++clock_reset_count_;
  builtin_interfaces::msg::Time stamp;
  if (!ToRosStamp(sample_realtime_ns, &stamp)) {
    AssignError(error, "mapped chassis odometry timestamp is outside ROS2 range");
    return false;
  }
  publisher_->publish(ToRosChassisOdometry(state, frame_id_, child_frame_id_, stamp));
  return true;
}

}  // namespace cockpit::bridge
