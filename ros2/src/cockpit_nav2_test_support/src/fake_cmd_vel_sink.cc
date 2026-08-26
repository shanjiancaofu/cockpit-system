#include <cstdint>
#include <geometry_msgs/msg/twist.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int64.hpp>

namespace {

class FakeCmdVelSink final : public rclcpp::Node {
 public:
  FakeCmdVelSink() : Node("cockpit_fake_cmd_vel_sink") {
    count_publisher_ = create_publisher<std_msgs::msg::UInt64>(
        "cockpit_nav2_test_support/cmd_vel_count", rclcpp::QoS(1).reliable().transient_local());
    subscription_ = create_subscription<geometry_msgs::msg::Twist>(
        "cmd_vel", 10, [this](const geometry_msgs::msg::Twist::ConstSharedPtr&) {
          std_msgs::msg::UInt64 count;
          count.data = ++command_count_;
          count_publisher_->publish(count);
        });
    std_msgs::msg::UInt64 initial_count;
    count_publisher_->publish(initial_count);
    RCLCPP_WARN(get_logger(), "test-only cmd_vel sink active; no hardware or CAN output exists");
  }

 private:
  rclcpp::Publisher<std_msgs::msg::UInt64>::SharedPtr count_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr subscription_;
  std::uint64_t command_count_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeCmdVelSink>());
  rclcpp::shutdown();
  return 0;
}
