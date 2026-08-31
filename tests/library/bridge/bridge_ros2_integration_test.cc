#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/library/bridge/ros2_camera_frame_adapter.h"
#include "cockpit/library/bridge/ros2_camera_info_adapter.h"
#include "cockpit/library/bridge/ros2_camera_publisher.h"
#include "cockpit/library/bridge/ros2_chassis_odometry_adapter.h"
#include "cockpit/library/bridge/ros2_chassis_odometry_publisher.h"
#include "cockpit/library/bridge/ros2_nav2_provider.h"
#include "cockpit/modules/camera/capture/synthetic_preview_source.h"

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
  explicit FakeNav2Server(rclcpp::Context::SharedPtr context,
                          std::chrono::milliseconds acknowledgement_delay = 0ms,
                          std::string action_name = "/cockpit_test_navigate_to_pose",
                          bool reject_cancellation = false)
      : context_(std::move(context)),
        acknowledgement_delay_(acknowledgement_delay),
        action_name_(std::move(action_name)),
        reject_cancellation_(reject_cancellation) {
    rclcpp::NodeOptions options;
    options.context(context_);
    node_ = std::make_shared<rclcpp::Node>("cockpit_fake_nav2_server", options);
    server_ = rclcpp_action::create_server<NavigateToPose>(
        node_, action_name_,
        [this](const rclcpp_action::GoalUUID&, std::shared_ptr<const NavigateToPose::Goal> goal) {
          if (acknowledgement_delay_ > 0ms) {
            std::this_thread::sleep_for(acknowledgement_delay_);
          }
          std::lock_guard lock(mutex_);
          last_frame_ = goal->pose.header.frame_id;
          last_x_ = goal->pose.pose.position.x;
          return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
        },
        [this](const std::shared_ptr<ServerGoalHandle>) {
          ++cancel_count_;
          return reject_cancellation_ ? rclcpp_action::CancelResponse::REJECT
                                      : rclcpp_action::CancelResponse::ACCEPT;
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

  void StopActionServer() {
    server_.reset();
    if (executor_ != nullptr && node_ != nullptr) {
      executor_->remove_node(node_);
    }
    node_.reset();
  }

  std::uint32_t cancel_count() const {
    return cancel_count_.load();
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
  std::chrono::milliseconds acknowledgement_delay_;
  std::string action_name_;
  bool reject_cancellation_ = false;
  std::atomic<std::uint32_t> cancel_count_{0};
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

  auto delayed_context = std::make_shared<rclcpp::Context>();
  delayed_context->init(0, nullptr);
  FakeNav2Server delayed_server(delayed_context, 150ms, "/cockpit_test_delayed_navigate_to_pose");
  cockpit::bridge::Ros2Nav2ProviderOptions delayed_options;
  delayed_options.action_name = "/cockpit_test_delayed_navigate_to_pose";
  delayed_options.server_timeout = 20ms;
  delayed_options.context = delayed_context;
  auto delayed_provider = cockpit::bridge::CreateRos2Nav2Provider(delayed_options, &error);
  Require(delayed_provider != nullptr, error);
  Require(WaitForState(delayed_provider.get(), cockpit::bridge::NavigationState::kIdle, 3s),
          "delayed provider did not become idle");
  cockpit::bridge::NavigationGoal delayed_goal;
  delayed_goal.goal_id = "ros2-delayed-ack";
  delayed_goal.target.frame_id = "map";
  delayed_goal.target.x_m = 2.0;
  const auto pending = delayed_provider->SubmitNavigationGoal(delayed_goal);
  Require(pending.state == cockpit::bridge::NavigationState::kDisconnected,
          std::string("delayed acknowledgement did not enter disconnected/pending state: ") +
              cockpit::bridge::NavigationStateName(pending.state));
  Require(delayed_provider->GetNavigationStatus().state ==
              cockpit::bridge::NavigationState::kDisconnected,
          "pending acknowledgement was incorrectly reset to idle");
  std::this_thread::sleep_for(300ms);
  Require(delayed_server.cancel_count() == 1,
          "late goal acknowledgement did not trigger internal cancellation without polling");
  Require(WaitForState(delayed_provider.get(), cockpit::bridge::NavigationState::kCancelled, 3s),
          "late accepted goal was not cancelled");

  auto rejected_cancel_context = std::make_shared<rclcpp::Context>();
  rejected_cancel_context->init(0, nullptr);
  FakeNav2Server rejected_cancel_server(rejected_cancel_context, 100ms,
                                        "/cockpit_test_rejected_cancel_navigate_to_pose", true);
  cockpit::bridge::Ros2Nav2ProviderOptions rejected_cancel_options;
  rejected_cancel_options.action_name = "/cockpit_test_rejected_cancel_navigate_to_pose";
  rejected_cancel_options.server_timeout = 20ms;
  rejected_cancel_options.context = rejected_cancel_context;
  auto rejected_cancel_provider =
      cockpit::bridge::CreateRos2Nav2Provider(rejected_cancel_options, &error);
  Require(rejected_cancel_provider != nullptr, error);
  Require(WaitForState(rejected_cancel_provider.get(), cockpit::bridge::NavigationState::kIdle, 3s),
          "rejected-cancel provider did not become idle");
  delayed_goal.goal_id = "ros2-rejected-cancel";
  const auto rejected_pending = rejected_cancel_provider->SubmitNavigationGoal(delayed_goal);
  Require(rejected_pending.state == cockpit::bridge::NavigationState::kDisconnected,
          "rejected-cancel fixture did not enter uncertain state");
  std::this_thread::sleep_for(150ms);
  const auto guarded = rejected_cancel_provider->SubmitNavigationGoal(delayed_goal);
  Require(guarded.state == cockpit::bridge::NavigationState::kDisconnected,
          "cancel rejection released uncertain-goal guard too early");

  auto idle_context = std::make_shared<rclcpp::Context>();
  idle_context->init(0, nullptr);
  FakeNav2Server idle_server(idle_context, 0ms, "/cockpit_test_idle_navigate_to_pose");
  cockpit::bridge::Ros2Nav2ProviderOptions idle_options;
  idle_options.action_name = "/cockpit_test_idle_navigate_to_pose";
  idle_options.server_timeout = 100ms;
  idle_options.context = idle_context;
  auto idle_provider = cockpit::bridge::CreateRos2Nav2Provider(idle_options, &error);
  Require(idle_provider != nullptr, error);
  Require(WaitForState(idle_provider.get(), cockpit::bridge::NavigationState::kIdle, 3s),
          "idle disconnect fixture did not become idle");
  idle_server.StopActionServer();
  Require(WaitForState(idle_provider.get(), cockpit::bridge::NavigationState::kDisconnected, 3s),
          "idle provider did not report Nav2 disconnect");

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
  source.k.pop_back();
  bool rejected_short_camera_info = false;
  try {
    static_cast<void>(cockpit::bridge::ToRosCameraInfo(source, "camera_optical", stamp));
  } catch (const std::invalid_argument&) {
    rejected_short_camera_info = true;
  }
  Require(rejected_short_camera_info, "short CameraInfo arrays were accepted");

  cockpit::hawkeye::CameraInfo runtime_info;
  runtime_info.width = 2;
  runtime_info.height = 2;
  runtime_info.distortion_model = "plumb_bob";
  runtime_info.d = {0, 0, 0, 0, 0};
  runtime_info.k = {1, 0, 1, 0, 1, 1, 0, 0, 1};
  runtime_info.r = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  runtime_info.p = {1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0};
  cockpit::bridge::Ros2CameraFrameAdapter frame_adapter(runtime_info, "camera_optical", true);
  cockpit::camera::CameraFrame frame;
  frame.sequence = 7;
  frame.width = 2;
  frame.height = 2;
  frame.stride_bytes = 8;
  frame.format = cockpit::camera::CameraPixelFormat::kBgrx;
  frame.source_timestamp_ns = 42000000007LL;
  frame.source_clock = cockpit::camera::CameraTimestampClock::kRealtime;
  frame.source_timestamp_valid = true;
  frame.data.assign(16, 128);
  cockpit::bridge::Ros2CameraFrameOutput frame_output;
  Require(frame_adapter.Convert(frame, &frame_output, &error),
          "CameraFrame ROS2 conversion failed: " + error);
  Require(frame_output.has_rectified_image && frame_output.image_raw.width == 2 &&
              frame_output.image_raw.height == 2 && frame_output.image_raw.header.stamp.sec == 42 &&
              frame_output.image_raw.header.stamp.nanosec == 7 &&
              frame_output.image_raw.header.stamp == frame_output.camera_info.header.stamp &&
              frame_output.image_raw.header.stamp == frame_output.image_rect.header.stamp &&
              frame_output.image_raw.data.size() == 12 && frame_output.image_rect.data.size() == 12,
          "CameraFrame timestamp or rectify output contract failed");

  cockpit::vehicle::ChassisState odometry_state;
  odometry_state.odometry_valid = true;
  odometry_state.x_mm = 1250;
  odometry_state.y_mm = -500;
  odometry_state.heading_mrad = 1571;
  odometry_state.odometry_linear_velocity_mm_s = 300;
  odometry_state.odometry_angular_velocity_mrad_s = -250;
  builtin_interfaces::msg::Time odometry_stamp;
  odometry_stamp.sec = 84;
  odometry_stamp.nanosec = 900;
  const auto ros_odometry =
      cockpit::bridge::ToRosChassisOdometry(odometry_state, "odom", "base_link", odometry_stamp);
  Require(ros_odometry.header.stamp == odometry_stamp && ros_odometry.header.frame_id == "odom" &&
              ros_odometry.child_frame_id == "base_link" &&
              std::abs(ros_odometry.pose.pose.position.x - 1.25) < 1e-9 &&
              std::abs(ros_odometry.pose.pose.position.y + 0.5) < 1e-9 &&
              std::abs(ros_odometry.twist.twist.linear.x - 0.3) < 1e-9 &&
              std::abs(ros_odometry.twist.twist.angular.z + 0.25) < 1e-9,
          "0x181 ChassisState to ROS Odometry conversion failed");
  odometry_state.odometry_valid = false;
  bool rejected_invalid_odometry = false;
  try {
    static_cast<void>(
        cockpit::bridge::ToRosChassisOdometry(odometry_state, "odom", "base_link", odometry_stamp));
  } catch (const std::invalid_argument&) {
    rejected_invalid_odometry = true;
  }
  Require(rejected_invalid_odometry, "invalid 0x181 odometry state was published");

  rclcpp::NodeOptions camera_node_options;
  camera_node_options.context(context);
  auto camera_publisher_node =
      std::make_shared<rclcpp::Node>("cockpit_camera_publisher_test", camera_node_options);
  auto camera_subscriber_node =
      std::make_shared<rclcpp::Node>("cockpit_camera_subscriber_test", camera_node_options);
  cockpit::bridge::Ros2CameraTopics camera_topics;
  camera_topics.image_raw = "/cockpit_test/camera/image_raw";
  camera_topics.camera_info = "/cockpit_test/camera/camera_info";
  camera_topics.image_rect = "/cockpit_test/camera/image_rect";
  cockpit::bridge::Ros2CameraPublisher camera_publisher(camera_publisher_node.get(), runtime_info,
                                                        "camera_optical", true, camera_topics);
  std::uint32_t raw_count = 0;
  std::uint32_t info_count = 0;
  std::uint32_t rect_count = 0;
  builtin_interfaces::msg::Time raw_stamp;
  builtin_interfaces::msg::Time info_stamp;
  builtin_interfaces::msg::Time rect_stamp;
  auto raw_subscription = camera_subscriber_node->create_subscription<sensor_msgs::msg::Image>(
      camera_topics.image_raw, rclcpp::SensorDataQoS(),
      [&](const sensor_msgs::msg::Image::ConstSharedPtr& message) {
        ++raw_count;
        raw_stamp = message->header.stamp;
      });
  auto info_subscription =
      camera_subscriber_node->create_subscription<sensor_msgs::msg::CameraInfo>(
          camera_topics.camera_info, rclcpp::SensorDataQoS(),
          [&](const sensor_msgs::msg::CameraInfo::ConstSharedPtr& message) {
            ++info_count;
            info_stamp = message->header.stamp;
          });
  auto rect_subscription = camera_subscriber_node->create_subscription<sensor_msgs::msg::Image>(
      camera_topics.image_rect, rclcpp::SensorDataQoS(),
      [&](const sensor_msgs::msg::Image::ConstSharedPtr& message) {
        ++rect_count;
        rect_stamp = message->header.stamp;
      });
  cockpit::bridge::Ros2ChassisOdometryPublisher odometry_publisher(camera_publisher_node.get(),
                                                                   "/cockpit_test/odom");
  std::uint32_t odometry_count = 0;
  nav_msgs::msg::Odometry received_odometry;
  auto odometry_subscription = camera_subscriber_node->create_subscription<nav_msgs::msg::Odometry>(
      "/cockpit_test/odom", 10, [&](const nav_msgs::msg::Odometry::ConstSharedPtr& message) {
        ++odometry_count;
        received_odometry = *message;
      });
  static_cast<void>(raw_subscription);
  static_cast<void>(info_subscription);
  static_cast<void>(rect_subscription);
  static_cast<void>(odometry_subscription);
  rclcpp::ExecutorOptions camera_executor_options;
  camera_executor_options.context = context;
  rclcpp::executors::SingleThreadedExecutor camera_executor(camera_executor_options);
  camera_executor.add_node(camera_subscriber_node);
  cockpit::camera::SyntheticPreviewSource synthetic_source;
  cockpit::camera::CameraPreviewConfig synthetic_config;
  synthetic_config.device = "synthetic://camera-publisher-test";
  synthetic_config.width = 2;
  synthetic_config.height = 2;
  synthetic_config.fps = 30;
  synthetic_config.output_format = cockpit::camera::CameraPixelFormat::kBgrx;
  std::atomic_bool camera_publish_failed{false};
  Require(synthetic_source.Start(
              synthetic_config,
              [&](cockpit::camera::CameraFrame synthetic_frame) {
                if (!camera_publisher.Publish(synthetic_frame, nullptr)) {
                  camera_publish_failed.store(true);
                }
              },
              &error),
          "synthetic camera source did not start: " + error);
  odometry_state.odometry_valid = true;
  for (int attempt = 0; attempt < 100 && (raw_count == 0 || info_count == 0 || rect_count == 0 ||
                                          odometry_count == 0);
       ++attempt) {
    odometry_state.odometry_timestamp_ms = static_cast<std::uint32_t>(1000 + attempt);
    Require(
        odometry_publisher.Publish(odometry_state, 100000000000LL + attempt * 1000000LL, &error),
        "live ROS Odometry publisher rejected 0x181 state: " + error);
    camera_executor.spin_some();
    std::this_thread::sleep_for(10ms);
  }
  odometry_state.peer_reboot_count = 1U;
  odometry_state.odometry_timestamp_ms = 5U;
  Require(odometry_publisher.Publish(odometry_state, 100200000000LL, &error) &&
              odometry_publisher.clock_reset_count() == 1U,
          "STM32 reboot evidence did not reset the live odometry clock mapper: " + error);
  odometry_state.odometry_timestamp_ms = 4U;
  Require(!odometry_publisher.Publish(odometry_state, 100201000000LL, &error),
          "unconfirmed odometry clock regression was accepted after the reboot baseline");
  synthetic_source.Stop();
  camera_executor.remove_node(camera_subscriber_node);
  Require(!camera_publish_failed.load() && raw_count > 0 && info_count > 0 && rect_count > 0 &&
              raw_stamp == info_stamp && raw_stamp == rect_stamp && raw_stamp.sec > 0,
          "live ROS Camera topics did not preserve a common sample timestamp");
  Require(odometry_count > 0 && received_odometry.header.frame_id == "odom" &&
              received_odometry.child_frame_id == "base_link" &&
              received_odometry.header.stamp.sec == 100 &&
              std::abs(received_odometry.pose.pose.position.x - 1.25) < 1e-9,
          "live 0x181 to /odom publisher contract failed");

  std::cout << "Bridge ROS2/Nav2 provider integration tests passed\n";
  return 0;
}
