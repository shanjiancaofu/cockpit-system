#include "cockpit_nav2_test_support/cmd_vel_safety.h"

#include <algorithm>

namespace cockpit::nav2_test_support {

geometry_msgs::msg::Twist BoundCommand(const geometry_msgs::msg::Twist& command) {
  geometry_msgs::msg::Twist bounded;
  bounded.linear.x = std::clamp(command.linear.x, -kMaxLinearVelocityMps, kMaxLinearVelocityMps);
  bounded.angular.z =
      std::clamp(command.angular.z, -kMaxAngularVelocityRadps, kMaxAngularVelocityRadps);
  return bounded;
}

bool IsCommandFresh(const rclcpp::Time& now, const rclcpp::Time& last_command) {
  return now >= last_command && now - last_command <= rclcpp::Duration(kCommandWatchdog);
}

}  // namespace cockpit::nav2_test_support
