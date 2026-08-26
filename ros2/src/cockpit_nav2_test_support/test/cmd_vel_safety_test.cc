#include "cockpit_nav2_test_support/cmd_vel_safety.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <rclcpp/rclcpp.hpp>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  geometry_msgs::msg::Twist command;
  command.linear.x = 3.0;
  command.linear.y = 2.0;
  command.angular.z = -5.0;
  const auto bounded = cockpit::nav2_test_support::BoundCommand(command);
  Require(std::abs(bounded.linear.x - 0.4) < 1e-9, "linear velocity was not bounded");
  Require(std::abs(bounded.linear.y) < 1e-9, "lateral velocity was not rejected");
  Require(std::abs(bounded.angular.z + 1.2) < 1e-9, "angular velocity was not bounded");

  const rclcpp::Time now(10, 0, RCL_ROS_TIME);
  Require(cockpit::nav2_test_support::IsCommandFresh(now, rclcpp::Time(9, 900000000, RCL_ROS_TIME)),
          "fresh command was rejected");
  Require(
      !cockpit::nav2_test_support::IsCommandFresh(now, rclcpp::Time(9, 700000000, RCL_ROS_TIME)),
      "stale command was accepted");
  rclcpp::shutdown();
  std::cout << "cmd_vel safety tests passed\n";
  return 0;
}
