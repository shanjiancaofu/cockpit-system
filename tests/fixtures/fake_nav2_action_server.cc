#include <signal.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using GoalHandle = rclcpp_action::ServerGoalHandle<NavigateToPose>;

std::atomic<bool> g_running{true};

void HandleSignal(int) {
  g_running.store(false, std::memory_order_relaxed);
}

class FakeNav2ActionServer {
 public:
  FakeNav2ActionServer(rclcpp::Node::SharedPtr node, std::string action_name)
      : node_(std::move(node)) {
    server_ = rclcpp_action::create_server<NavigateToPose>(
        node_, std::move(action_name),
        [](const rclcpp_action::GoalUUID&, std::shared_ptr<const NavigateToPose::Goal> goal) {
          const auto& pose = goal->pose.pose;
          if (!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y)) {
            return rclcpp_action::GoalResponse::REJECT;
          }
          return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
        },
        [](const std::shared_ptr<GoalHandle>) {
          return rclcpp_action::CancelResponse::ACCEPT;
        },
        [this](const std::shared_ptr<GoalHandle> handle) {
          std::lock_guard lock(workers_mutex_);
          workers_.emplace_back([this, handle] {
            Execute(handle);
          });
        });
  }

  ~FakeNav2ActionServer() {
    std::lock_guard lock(workers_mutex_);
    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

 private:
  void Execute(const std::shared_ptr<GoalHandle>& handle) {
    const auto goal = handle->get_goal();
    const bool stalled = goal->pose.pose.position.x < 0.0;
    for (int iteration = 0; g_running.load(std::memory_order_relaxed) && rclcpp::ok();
         ++iteration) {
      if (handle->is_canceling()) {
        handle->canceled(std::make_shared<NavigateToPose::Result>());
        return;
      }
      auto feedback = std::make_shared<NavigateToPose::Feedback>();
      feedback->current_pose.header.frame_id = goal->pose.header.frame_id;
      feedback->current_pose.header.stamp = node_->now();
      feedback->current_pose.pose.position.x = goal->pose.pose.position.x / 2.0;
      feedback->current_pose.pose.position.y = goal->pose.pose.position.y / 2.0;
      feedback->current_pose.pose.orientation = goal->pose.pose.orientation;
      handle->publish_feedback(feedback);
      if (!stalled && iteration >= 10) {
        handle->succeed(std::make_shared<NavigateToPose::Result>());
        return;
      }
      std::this_thread::sleep_for(50ms);
    }
  }

  rclcpp::Node::SharedPtr node_;
  rclcpp_action::Server<NavigateToPose>::SharedPtr server_;
  std::mutex workers_mutex_;
  std::vector<std::thread> workers_;
};

}  // namespace

int main(int argc, char** argv) {
  std::string action_name = "/cockpit_smoke_navigate_to_pose";
  if (argc == 3 && std::string(argv[1]) == "--action-name") {
    action_name = argv[2];
  } else if (argc != 1) {
    std::cerr << "usage: fake_nav2_action_server [--action-name /absolute/name]\n";
    return 2;
  }
  if (action_name.empty() || action_name.front() != '/') {
    std::cerr << "action name must be absolute\n";
    return 2;
  }

  struct sigaction action {};
  action.sa_handler = HandleSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);

  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("cockpit_fake_nav2_action_server");
  FakeNav2ActionServer server(node, action_name);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  std::cout << "fake Nav2 action server ready: " << action_name << std::endl;
  while (g_running.load(std::memory_order_relaxed) && rclcpp::ok()) {
    executor.spin_some();
    std::this_thread::sleep_for(10ms);
  }
  executor.cancel();
  rclcpp::shutdown();
  return 0;
}
