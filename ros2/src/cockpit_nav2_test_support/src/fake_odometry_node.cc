#include <tf2/LinearMath/Quaternion.h>
#include <tf2_ros/transform_broadcaster.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <memory>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

namespace {

using namespace std::chrono_literals;

class FakeOdometryNode final : public rclcpp::Node {
 public:
  FakeOdometryNode()
      : Node("cockpit_fake_odometry"),
        transform_broadcaster_(*this),
        last_update_(now()),
        last_command_(now()) {
    odometry_publisher_ = create_publisher<nav_msgs::msg::Odometry>("odom", 10);
    velocity_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
        "cmd_vel", 10, [this](const geometry_msgs::msg::Twist::ConstSharedPtr& message) {
          linear_velocity_mps_ = std::clamp(message->linear.x, -0.4, 0.4);
          angular_velocity_radps_ = std::clamp(message->angular.z, -1.2, 1.2);
          last_command_ = now();
        });
    timer_ = create_wall_timer(50ms, [this] {
      Update();
    });
    // rclcpp Humble requires shared_ptr service callback parameters by value.
    // NOLINTBEGIN(performance-unnecessary-value-param)
    control_service_ = create_service<std_srvs::srv::SetBool>(
        "cockpit_nav2_test_support/set_odometry_enabled",
        [this](const std_srvs::srv::SetBool::Request::SharedPtr request,
               std_srvs::srv::SetBool::Response::SharedPtr response) {
          enabled_ = request->data;
          linear_velocity_mps_ = 0.0;
          angular_velocity_radps_ = 0.0;
          last_update_ = now();
          last_command_ = last_update_;
          response->success = true;
          response->message = enabled_ ? "fake odometry enabled" : "fake odometry disabled";
        });
    // NOLINTEND(performance-unnecessary-value-param)
  }

 private:
  geometry_msgs::msg::Quaternion Orientation() const {
    tf2::Quaternion quaternion;
    quaternion.setRPY(0.0, 0.0, yaw_rad_);
    return tf2::toMsg(quaternion);
  }

  void Update() {
    if (!enabled_) {
      return;
    }
    const rclcpp::Time current_time = now();
    const double elapsed_seconds = std::clamp((current_time - last_update_).seconds(), 0.0, 0.1);
    last_update_ = current_time;
    if ((current_time - last_command_).seconds() > 0.25) {
      linear_velocity_mps_ = 0.0;
      angular_velocity_radps_ = 0.0;
    }
    x_m_ += std::cos(yaw_rad_) * linear_velocity_mps_ * elapsed_seconds;
    y_m_ += std::sin(yaw_rad_) * linear_velocity_mps_ * elapsed_seconds;
    yaw_rad_ += angular_velocity_radps_ * elapsed_seconds;

    const auto orientation = Orientation();
    geometry_msgs::msg::TransformStamped transform;
    transform.header.stamp = current_time;
    transform.header.frame_id = "odom";
    transform.child_frame_id = "base_link";
    transform.transform.translation.x = x_m_;
    transform.transform.translation.y = y_m_;
    transform.transform.rotation = orientation;
    transform_broadcaster_.sendTransform(transform);

    nav_msgs::msg::Odometry odometry;
    odometry.header = transform.header;
    odometry.child_frame_id = transform.child_frame_id;
    odometry.pose.pose.position.x = x_m_;
    odometry.pose.pose.position.y = y_m_;
    odometry.pose.pose.orientation = orientation;
    odometry.twist.twist.linear.x = linear_velocity_mps_;
    odometry.twist.twist.angular.z = angular_velocity_radps_;
    odometry_publisher_->publish(odometry);
  }

  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odometry_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr velocity_subscription_;
  tf2_ros::TransformBroadcaster transform_broadcaster_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr control_service_;
  rclcpp::Time last_update_;
  rclcpp::Time last_command_;
  double x_m_ = 0.0;
  double y_m_ = 0.0;
  double yaw_rad_ = 0.0;
  double linear_velocity_mps_ = 0.0;
  double angular_velocity_radps_ = 0.0;
  bool enabled_ = true;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeOdometryNode>());
  rclcpp::shutdown();
  return 0;
}
