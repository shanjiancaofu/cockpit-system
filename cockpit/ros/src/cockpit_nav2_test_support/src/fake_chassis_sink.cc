#include <cmath>
#include <cstdint>
#include <geometry_msgs/msg/twist.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/u_int64.hpp>

namespace {

class FakeChassisSink final : public rclcpp::Node {
 public:
  FakeChassisSink() : Node("cockpit_fake_chassis_sink") {
    count_publisher_ = create_publisher<std_msgs::msg::UInt64>(
        "cockpit_nav2_test_support/cmd_vel_count", rclcpp::QoS(1).reliable().transient_local());
    nonzero_publisher_ = create_publisher<std_msgs::msg::Bool>(
        "cockpit_nav2_test_support/cmd_vel_nonzero", rclcpp::QoS(1).reliable().transient_local());
    safety_status_publisher_ = create_publisher<std_msgs::msg::String>(
        "cockpit_nav2_test_support/safety_status", rclcpp::QoS(1).reliable().transient_local());
    subscription_ = create_subscription<geometry_msgs::msg::Twist>(
        "cmd_vel_safe", 10, [this](const geometry_msgs::msg::Twist::ConstSharedPtr& message) {
          std_msgs::msg::UInt64 count;
          count.data = ++command_count_;
          count_publisher_->publish(count);
          std_msgs::msg::Bool nonzero;
          nonzero.data = std::abs(message->linear.x) > 1.0e-6 ||
                         std::abs(message->linear.y) > 1.0e-6 ||
                         std::abs(message->angular.z) > 1.0e-6;
          nonzero_publisher_->publish(nonzero);
        });
    safety_status_subscription_ = create_subscription<std_msgs::msg::String>(
        "chassis_safety/status", 10, [this](const std_msgs::msg::String::ConstSharedPtr& message) {
          safety_status_publisher_->publish(*message);
        });
    std_msgs::msg::UInt64 initial_count;
    count_publisher_->publish(initial_count);
    std_msgs::msg::Bool initial_nonzero;
    nonzero_publisher_->publish(initial_nonzero);
    RCLCPP_WARN(get_logger(), "FakeChassis active; no hardware or CAN output exists");
  }

 private:
  rclcpp::Publisher<std_msgs::msg::UInt64>::SharedPtr count_publisher_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr nonzero_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr safety_status_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr safety_status_subscription_;
  std::uint64_t command_count_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeChassisSink>());
  rclcpp::shutdown();
  return 0;
}
