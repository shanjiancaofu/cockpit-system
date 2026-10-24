#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <lifecycle_msgs/msg/state.hpp>
#include <lifecycle_msgs/srv/get_state.hpp>
#include <memory>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
using NavigateToPose = nav2_msgs::action::NavigateToPose;

std::optional<std::uint8_t> LifecycleState(const rclcpp::Node::SharedPtr& node,
                                           const std::string& node_name) {
  auto client = node->create_client<lifecycle_msgs::srv::GetState>(node_name + "/get_state");
  if (!client->wait_for_service(200ms)) {
    return std::nullopt;
  }
  auto future =
      client->async_send_request(std::make_shared<lifecycle_msgs::srv::GetState::Request>());
  if (rclcpp::spin_until_future_complete(node, future, 500ms) !=
      rclcpp::FutureReturnCode::SUCCESS) {
    return std::nullopt;
  }
  return future.get()->current_state.id;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("cockpit_nav2_readiness_probe");
    auto action_client = rclcpp_action::create_client<NavigateToPose>(node, "/navigate_to_pose");
    if (!action_client->wait_for_action_server(20s)) {
      std::cerr << "NavigateToPose action server is unavailable\n";
      rclcpp::shutdown();
      return 1;
    }
    constexpr std::array<const char*, 4> lifecycle_nodes{"/map_server", "/controller_server",
                                                         "/planner_server", "/bt_navigator"};
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < deadline) {
      bool all_active = true;
      for (const char* lifecycle_node : lifecycle_nodes) {
        const auto state = LifecycleState(node, lifecycle_node);
        all_active &=
            state.has_value() && *state == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE;
      }
      if (all_active) {
        std::cout << "Nav2 lifecycle and NavigateToPose action are ready\n";
        rclcpp::shutdown();
        return 0;
      }
      std::this_thread::sleep_for(100ms);
    }
    std::cerr << "Nav2 lifecycle nodes did not all become active\n";
    rclcpp::shutdown();
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "Nav2 readiness probe failed: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
