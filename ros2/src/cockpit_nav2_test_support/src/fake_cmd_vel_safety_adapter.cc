#include <algorithm>
#include <chrono>
#include <cmath>
#include <geometry_msgs/msg/twist.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "cockpit_nav2_test_support/cmd_vel_safety.h"

namespace {

using namespace std::chrono_literals;

class FakeCmdVelSafetyAdapter final : public rclcpp::Node {
 public:
  FakeCmdVelSafetyAdapter() : Node("cockpit_fake_cmd_vel_safety_adapter"), last_command_(now()) {
    publisher_ = create_publisher<geometry_msgs::msg::Twist>("cmd_vel_safe", 10);
    limit_publisher_ = create_publisher<geometry_msgs::msg::Twist>("cmd_vel_safe_last", 1);
    subscription_ = create_subscription<geometry_msgs::msg::Twist>(
        "cmd_vel", 10, [this](const geometry_msgs::msg::Twist::ConstSharedPtr& message) {
          command_ = *message;
          last_command_ = now();
        });
    timer_ = create_wall_timer(20ms, [this] {
      Publish();
    });
    RCLCPP_WARN(get_logger(),
                "test-only cmd_vel safety adapter active; output is bounded and never reaches CAN");
  }

 private:
  void Publish() {
    geometry_msgs::msg::Twist safe_command;
    const bool fresh = cockpit::nav2_test_support::IsCommandFresh(now(), last_command_);
    if (fresh) {
      safe_command = cockpit::nav2_test_support::BoundCommand(command_);
    }
    publisher_->publish(safe_command);
    limit_publisher_->publish(safe_command);
  }

  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr publisher_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr limit_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
  geometry_msgs::msg::Twist command_;
  rclcpp::Time last_command_;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeCmdVelSafetyAdapter>());
  rclcpp::shutdown();
  return 0;
}
