#pragma once

#include <chrono>
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>

namespace cockpit::nav2_test_support {

constexpr double kMaxLinearVelocityMps = 0.4;
constexpr double kMaxAngularVelocityRadps = 1.2;
constexpr std::chrono::milliseconds kCommandWatchdog{250};

geometry_msgs::msg::Twist BoundCommand(const geometry_msgs::msg::Twist& command);
bool IsCommandFresh(const rclcpp::Time& now, const rclcpp::Time& last_command);

}  // namespace cockpit::nav2_test_support
