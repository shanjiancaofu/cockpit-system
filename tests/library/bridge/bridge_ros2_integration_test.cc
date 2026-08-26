#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/library/bridge/ros2_camera_info_adapter.h"
#include "cockpit/library/bridge/ros2_nav2_provider.h"

namespace {

using namespace std::chrono_literals;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using ServerGoalHandle = rclcpp_action::ServerGoalHandle<NavigateToPose>;

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "bridge ROS2 integration test failed: " << message << '\n';
    std::exit(1);
  }
}

class FakeNav2Server {
 public:
  explicit FakeNav2Server(rclcpp::Context::SharedPtr context) : context_(std::move(context)) {
    rclcpp::NodeOptions options;
    options.context(context_);
    node_ = std::make_shared<rclcpp::Node>("cockpit_fake_nav2_server", options);
    server_ = rclcpp_action::create_server<NavigateToPose>(
        node_, "/cockpit_test_navigate_to_pose",
        [this](const rclcpp_action::GoalUUID&, std::shared_ptr<const NavigateToPose::Goal> goal) {
          std::lock_guard lock(mutex_);
          last_frame_ = goal->pose.header.frame_id;
          last_x_ = goal->pose.pose.position.x;
          return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
        },
        [](const std::shared_ptr<ServerGoalHandle>) {
          return rclcpp_action::CancelResponse::ACCEPT;
        },
        [this](const std::shared_ptr<ServerGoalHandle> handle) {
          std::lock_guard lock(worker_mutex_);
          workers_.emplace_back([handle] {
            std::this_thread::sleep_for(50ms);
            if (handle->is_canceling()) {
              handle->canceled(std::make_shared<NavigateToPose::Result>());
              return;
            }
            auto feedback = std::make_shared<NavigateToPose::Feedback>();
            feedback->current_pose.header.frame_id = "map";
            feedback->current_pose.header.stamp.sec = 123;
            feedback->current_pose.header.stamp.nanosec = 456000000;
            feedback->current_pose.pose.position.x = 1.25;
            feedback->current_pose.pose.position.y = -0.5;
            feedback->current_pose.pose.orientation.w = 1.0;
            handle->publish_feedback(feedback);
            std::this_thread::sleep_for(20ms);
            handle->succeed(std::make_shared<NavigateToPose::Result>());
          });
        });
    rclcpp::ExecutorOptions executor_options;
    executor_options.context = context_;
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
    executor_->add_node(node_);
    spin_thread_ = std::thread([this] {
      executor_->spin();
    });
  }

  ~FakeNav2Server() {
    {
      std::lock_guard lock(worker_mutex_);
      for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
      }
    }
    executor_->cancel();
    context_->shutdown("test complete");
    if (spin_thread_.joinable()) spin_thread_.join();
  }

  std::string last_frame() const {
    std::lock_guard lock(mutex_);
    return last_frame_;
  }
  double last_x() const {
    std::lock_guard lock(mutex_);
    return last_x_;
  }

 private:
  rclcpp::Context::SharedPtr context_;
  rclcpp::Node::SharedPtr node_;
  rclcpp_action::Server<NavigateToPose>::SharedPtr server_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_thread_;
  mutable std::mutex mutex_;
  std::string last_frame_;
  double last_x_ = 0.0;
  std::mutex worker_mutex_;
  std::vector<std::thread> workers_;
};

bool WaitForState(cockpit::bridge::NavigationProvider* provider,
                  cockpit::bridge::NavigationState state, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (provider->GetNavigationStatus().state == state) return true;
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

}  // namespace

int main() {
  auto context = std::make_shared<rclcpp::Context>();
  context->init(0, nullptr);
  FakeNav2Server server(context);
  cockpit::bridge::Ros2Nav2ProviderOptions options;
  options.action_name = "/cockpit_test_navigate_to_pose";
  options.server_timeout = 3s;
  options.context = context;
  std::string error;
  auto provider = cockpit::bridge::CreateRos2Nav2Provider(options, &error);
  Require(provider != nullptr, error);
  Require(WaitForState(provider.get(), cockpit::bridge::NavigationState::kIdle, 3s),
          "provider did not recover from disconnected to idle");

  cockpit::bridge::NavigationGoal goal;
  goal.goal_id = "ros2-goal-1";
  goal.target.frame_id = "map";
  goal.target.x_m = 2.5;
  goal.target.y_m = -1.0;
  goal.target.yaw_rad = 0.5;
  const auto accepted = provider->SubmitNavigationGoal(goal);
  Require(accepted.state == cockpit::bridge::NavigationState::kAccepted,
          std::string("goal was not accepted: state=") +
              cockpit::bridge::NavigationStateName(accepted.state) +
              " error=" + accepted.last_error);
  Require(WaitForState(provider.get(), cockpit::bridge::NavigationState::kSucceeded, 3s),
          "goal did not succeed");
  const auto succeeded = provider->GetNavigationStatus();
  Require(succeeded.current_pose_valid, "feedback pose is not valid");
  Require(succeeded.current_pose.timestamp_ms == 123456, "ROS header timestamp was not preserved");
  Require(server.last_frame() == "map" && std::abs(server.last_x() - 2.5) < 1e-9,
          "goal pose was not forwarded");

  goal.goal_id = "ros2-goal-2";
  Require(provider->SubmitNavigationGoal(goal).state == cockpit::bridge::NavigationState::kAccepted,
          "cancel fixture goal was not accepted");
  Require(provider->CancelNavigationGoal(goal.goal_id).state ==
              cockpit::bridge::NavigationState::kCancelled,
          "goal cancellation was not confirmed");

  cockpit::hawkeye::CameraInfo source;
  source.width = 1280;
  source.height = 720;
  source.distortion_model = "plumb_bob";
  source.d = {1, 2, 3, 4, 5};
  source.k = {10, 0, 20, 0, 11, 21, 0, 0, 1};
  source.r = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  source.p = {10, 0, 20, 0, 0, 11, 21, 0, 0, 0, 1, 0};
  builtin_interfaces::msg::Time stamp;
  stamp.sec = 42;
  stamp.nanosec = 7;
  const auto ros_info = cockpit::bridge::ToRosCameraInfo(source, "camera_optical", stamp);
  Require(ros_info.header.frame_id == "camera_optical" && ros_info.header.stamp.sec == 42,
          "CameraInfo header conversion failed");
  Require(ros_info.width == 1280 && ros_info.k[0] == 10 && ros_info.p[6] == 21,
          "CameraInfo calibration conversion failed");

  std::cout << "Bridge ROS2/Nav2 provider integration tests passed\n";
  return 0;
}
