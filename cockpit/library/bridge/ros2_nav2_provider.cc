#include "cockpit/library/bridge/ros2_nav2_provider.h"

#include <action_msgs/srv/cancel_goal.hpp>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <thread>
#include <utility>

namespace cockpit::bridge {
namespace {

using NavigateToPose = nav2_msgs::action::NavigateToPose;
using GoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;
using CancelGoal = action_msgs::srv::CancelGoal;

std::int64_t ToMilliseconds(const builtin_interfaces::msg::Time& stamp) {
  return (static_cast<std::int64_t>(stamp.sec) * 1000) + (stamp.nanosec / 1000000);
}

double ToYaw(const geometry_msgs::msg::Quaternion& quaternion) {
  const double siny_cosp = 2.0 * ((quaternion.w * quaternion.z) + (quaternion.x * quaternion.y));
  const double cosy_cosp =
      1.0 - (2.0 * ((quaternion.y * quaternion.y) + (quaternion.z * quaternion.z)));
  return std::atan2(siny_cosp, cosy_cosp);
}

class Ros2Nav2Provider final : public NavigationProvider {
 public:
  Ros2Nav2Provider(Ros2Nav2ProviderOptions options, std::string* error)
      : options_(std::move(options)),
        context_(options_.context != nullptr ? options_.context
                                             : std::make_shared<rclcpp::Context>()),
        owns_context_(options_.context == nullptr) {
    try {
      if (owns_context_) {
        context_->init(0, nullptr);
      }
      rclcpp::NodeOptions node_options;
      node_options.context(context_);
      node_ = std::make_shared<rclcpp::Node>("cockpit_bridge_nav2_client", node_options);
      client_ = rclcpp_action::create_client<NavigateToPose>(node_, options_.action_name);
      rclcpp::ExecutorOptions executor_options;
      executor_options.context = context_;
      executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
      executor_->add_node(node_);
      spin_thread_ = std::thread([this] {
        executor_->spin();
      });
      status_.state = NavigationState::kDisconnected;
      status_.message = "waiting for Nav2 action server";
    } catch (const std::exception& exception) {
      *error = std::string("failed to initialize ROS2 Nav2 provider: ") + exception.what();
      Shutdown();
    }
  }

  ~Ros2Nav2Provider() override {
    Shutdown();
  }

  bool initialized() const {
    return node_ != nullptr && client_ != nullptr && spin_thread_.joinable();
  }

  NavigationStatus SubmitNavigationGoal(const NavigationGoal& goal) override {
    if (!client_->wait_for_action_server(options_.server_timeout)) {
      std::lock_guard lock(mutex_);
      status_ = NavigationStatus{};
      status_.state = NavigationState::kDisconnected;
      status_.goal_id = goal.goal_id;
      status_.target = goal.target;
      status_.last_error = "Nav2 action server unavailable";
      return status_;
    }

    NavigateToPose::Goal ros_goal;
    ros_goal.pose.header.frame_id = goal.target.frame_id;
    ros_goal.pose.header.stamp = node_->now();
    ros_goal.pose.pose.position.x = goal.target.x_m;
    ros_goal.pose.pose.position.y = goal.target.y_m;
    ros_goal.pose.pose.orientation.z = std::sin(goal.target.yaw_rad / 2.0);
    ros_goal.pose.pose.orientation.w = std::cos(goal.target.yaw_rad / 2.0);

    std::uint64_t generation;
    {
      std::lock_guard lock(mutex_);
      if (pending_ack_ || pending_cancel_ || IsActiveNavigationState(status_.state)) {
        status_.last_error = "Nav2 goal acknowledgement is still pending";
        return status_;
      }
      generation = ++generation_;
      status_ = NavigationStatus{};
      status_.state = NavigationState::kAccepted;
      status_.goal_id = goal.goal_id;
      status_.target = goal.target;
      status_.message = "waiting for Nav2 goal acknowledgement";
    }

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions send_options;
    send_options.goal_response_callback = [this, generation](GoalHandle::SharedPtr handle) {
      std::lock_guard lock(mutex_);
      HandleGoalResponseLocked(generation, std::move(handle));
      status_changed_.notify_all();
    };
    send_options.feedback_callback =
        [this, generation](const GoalHandle::SharedPtr&,
                           const std::shared_ptr<const NavigateToPose::Feedback>& feedback) {
          std::lock_guard lock(mutex_);
          if (generation != generation_ || !IsActiveNavigationState(status_.state)) {
            return;
          }
          const auto& pose = feedback->current_pose;
          status_.state = NavigationState::kExecuting;
          status_.current_pose.x_m = pose.pose.position.x;
          status_.current_pose.y_m = pose.pose.position.y;
          status_.current_pose.yaw_rad = ToYaw(pose.pose.orientation);
          status_.current_pose.frame_id = pose.header.frame_id;
          status_.current_pose.timestamp_ms = ToMilliseconds(pose.header.stamp);
          status_.current_pose_valid = true;
          status_.message = "Nav2 navigation executing";
        };
    send_options.result_callback = [this, generation](const GoalHandle::WrappedResult& result) {
      {
        std::lock_guard lock(mutex_);
        if (generation != generation_) {
          return;
        }
        pending_cancel_ = false;
        pending_cancel_handle_.reset();
        pending_cancel_future_ = {};
        switch (result.code) {
          case rclcpp_action::ResultCode::SUCCEEDED:
            status_.state = NavigationState::kSucceeded;
            status_.message = "Nav2 navigation succeeded";
            status_.last_error.clear();
            break;
          case rclcpp_action::ResultCode::CANCELED:
            status_.state = NavigationState::kCancelled;
            status_.message = "Nav2 navigation cancelled";
            break;
          case rclcpp_action::ResultCode::ABORTED:
            status_.state = NavigationState::kFailed;
            status_.last_error = "Nav2 navigation aborted";
            break;
          default:
            status_.state = NavigationState::kFailed;
            status_.last_error = "Nav2 returned an unknown result";
            break;
        }
        goal_handle_.reset();
      }
      status_changed_.notify_all();
    };

    {
      std::lock_guard lock(mutex_);
      pending_ack_ = true;
      pending_generation_ = generation;
    }
    auto future = client_->async_send_goal(ros_goal, send_options);
    {
      std::lock_guard lock(mutex_);
      if (pending_ack_ && pending_generation_ == generation) {
        pending_ack_future_ = future;
      }
    }
    if (future.wait_for(options_.server_timeout) != std::future_status::ready) {
      std::lock_guard lock(mutex_);
      status_.state = NavigationState::kDisconnected;
      status_.message = "Nav2 goal acknowledgement pending; refusing new goals";
      status_.last_error =
          "Nav2 goal acknowledgement timed out; cancellation will be attempted if accepted";
      return status_;
    }
    std::lock_guard lock(mutex_);
    if (pending_ack_ && pending_generation_ == generation) {
      HandleGoalResponseLocked(generation, future.get());
    }
    return status_;
  }

  NavigationStatus CancelNavigationGoal(const std::string& goal_id) override {
    GoalHandle::SharedPtr handle;
    {
      std::lock_guard lock(mutex_);
      if (goal_id != status_.goal_id || !IsActiveNavigationState(status_.state)) {
        return status_;
      }
      handle = goal_handle_;
    }
    if (handle == nullptr) {
      return GetNavigationStatus();
    }
    auto future = client_->async_cancel_goal(handle);
    if (future.wait_for(options_.server_timeout) != std::future_status::ready) {
      std::lock_guard lock(mutex_);
      status_.last_error = "Nav2 cancel acknowledgement timed out";
      return status_;
    }
    const auto& response = future.get();
    std::unique_lock lock(mutex_);
    if (response->goals_canceling.empty()) {
      status_.last_error = "Nav2 rejected navigation cancellation";
      return status_;
    }
    status_.message = "waiting for Nav2 cancellation result";
    if (!status_changed_.wait_for(lock, options_.server_timeout, [this] {
          return !IsActiveNavigationState(status_.state);
        })) {
      status_.last_error = "Nav2 cancellation result timed out";
    }
    return status_;
  }

  NavigationStatus GetNavigationStatus() override {
    const bool server_ready = client_->action_server_is_ready();
    std::lock_guard lock(mutex_);
    ResolvePendingAckLocked();
    if (pending_cancel_ && pending_cancel_future_.valid() &&
        pending_cancel_future_.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
      const auto& response = pending_cancel_future_.get();
      if (response->goals_canceling.empty()) {
        status_.state = NavigationState::kDisconnected;
        status_.message = "Nav2 rejected cancellation of uncertain goal; retaining guard";
        status_.last_error = status_.message;
      }
      pending_cancel_future_ = {};
    }
    if (pending_ack_ || pending_cancel_) {
      return status_;
    }
    if (status_.state == NavigationState::kIdle && !server_ready) {
      status_.state = NavigationState::kDisconnected;
      status_.message = "Nav2 action server unavailable";
      status_.last_error = status_.message;
    } else if (status_.state == NavigationState::kDisconnected && server_ready) {
      status_ = NavigationStatus{};
      status_.state = NavigationState::kIdle;
      status_.message = "Nav2 action server ready";
    }
    return status_;
  }

 private:
  void HandleGoalResponseLocked(std::uint64_t generation, GoalHandle::SharedPtr handle) {
    if (generation != generation_) {
      return;
    }
    pending_ack_ = false;
    pending_ack_future_ = {};
    if (handle == nullptr) {
      status_.state = NavigationState::kRejected;
      status_.last_error = "Nav2 rejected navigation goal";
      return;
    }
    goal_handle_ = std::move(handle);
    if (status_.state == NavigationState::kDisconnected) {
      pending_cancel_ = true;
      pending_cancel_handle_ = goal_handle_;
      status_.message = "delayed Nav2 goal accepted; cancelling uncertain goal";
      status_.last_error = "goal acknowledgement timed out; cancellation requested";
      pending_cancel_future_ = client_->async_cancel_goal(goal_handle_);
    } else if (IsActiveNavigationState(status_.state)) {
      status_.message = "Nav2 accepted navigation goal";
    }
  }

  void ResolvePendingAckLocked() {
    if (!pending_ack_ ||
        pending_ack_future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }
    const auto handle = pending_ack_future_.get();
    pending_ack_ = false;
    pending_ack_future_ = {};
    if (pending_generation_ != generation_) {
      return;
    }
    if (handle == nullptr) {
      status_.state = NavigationState::kRejected;
      status_.last_error = "Nav2 rejected delayed navigation goal";
      status_changed_.notify_all();
      return;
    }
    goal_handle_ = handle;
    pending_cancel_ = true;
    pending_cancel_handle_ = handle;
    status_.state = NavigationState::kDisconnected;
    status_.message = "delayed Nav2 goal accepted; cancelling uncertain goal";
    status_.last_error = "goal acknowledgement timed out; cancellation requested";
    pending_cancel_future_ = client_->async_cancel_goal(handle);
    status_changed_.notify_all();
  }

  void Shutdown() {
    if (executor_ != nullptr) {
      executor_->cancel();
    }
    if (owns_context_ && context_ != nullptr && context_->is_valid()) {
      context_->shutdown("provider shutdown");
    }
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
    executor_.reset();
    client_.reset();
    node_.reset();
  }

  Ros2Nav2ProviderOptions options_;
  rclcpp::Context::SharedPtr context_;
  bool owns_context_ = false;
  rclcpp::Node::SharedPtr node_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr client_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_thread_;
  mutable std::mutex mutex_;
  std::condition_variable status_changed_;
  NavigationStatus status_;
  GoalHandle::SharedPtr goal_handle_;
  std::uint64_t generation_ = 0;
  std::uint64_t pending_generation_ = 0;
  bool pending_ack_ = false;
  bool pending_cancel_ = false;
  std::shared_future<GoalHandle::SharedPtr> pending_ack_future_;
  std::shared_future<std::shared_ptr<CancelGoal::Response>> pending_cancel_future_;
  GoalHandle::SharedPtr pending_cancel_handle_;
};

}  // namespace

std::unique_ptr<NavigationProvider> CreateRos2Nav2Provider(const Ros2Nav2ProviderOptions& options,
                                                           std::string* error) {
  if (error == nullptr) {
    return nullptr;
  }
  error->clear();
  if (options.action_name.empty() || options.server_timeout.count() <= 0) {
    *error = "invalid ROS2 Nav2 provider options";
    return nullptr;
  }
  auto provider = std::make_unique<Ros2Nav2Provider>(options, error);
  if (!provider->initialized()) {
    return nullptr;
  }
  return provider;
}

}  // namespace cockpit::bridge
