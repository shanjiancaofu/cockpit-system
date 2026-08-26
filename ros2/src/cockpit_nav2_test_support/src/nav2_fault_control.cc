#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

template <typename Service>
bool WaitForResponse(const rclcpp::Node::SharedPtr& node,
                     const typename rclcpp::Client<Service>::SharedPtr& client,
                     const std::shared_ptr<typename Service::Request>& request) {
  if (!client->wait_for_service(3s)) {
    return false;
  }
  auto future = client->async_send_request(request);
  return rclcpp::spin_until_future_complete(node, future, 3s) ==
             rclcpp::FutureReturnCode::SUCCESS &&
         future.get()->success;
}

bool SetOdometry(const rclcpp::Node::SharedPtr& node, bool enabled) {
  auto client = node->create_client<std_srvs::srv::SetBool>(
      "/cockpit_nav2_test_support/set_odometry_enabled");
  auto request = std::make_shared<std_srvs::srv::SetBool::Request>();
  request->data = enabled;
  return WaitForResponse<std_srvs::srv::SetBool>(node, client, request);
}

bool AssertCmdVelZero(const rclcpp::Node::SharedPtr& node) {
  bool received = false;
  bool nonzero = true;
  auto subscription = node->create_subscription<std_msgs::msg::Bool>(
      "/cockpit_nav2_test_support/cmd_vel_nonzero", rclcpp::QoS(1).reliable().transient_local(),
      [&received, &nonzero](const std_msgs::msg::Bool::ConstSharedPtr& message) {
        received = true;
        nonzero = message->data;
      });
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && (!received || nonzero)) {
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(20ms);
  }
  static_cast<void>(subscription);
  return received && !nonzero;
}

int Run(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: nav2_fault_control "
                 "odometry-enable|odometry-disable|assert-cmd-zero\n";
    return 2;
  }
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("cockpit_nav2_fault_control");
  const std::string command = argv[1];
  bool success = false;
  if (command == "odometry-enable") {
    success = SetOdometry(node, true);
  } else if (command == "odometry-disable") {
    success = SetOdometry(node, false);
  } else if (command == "assert-cmd-zero") {
    success = AssertCmdVelZero(node);
  } else {
    std::cerr << "unknown command: " << command << '\n';
    rclcpp::shutdown();
    return 2;
  }
  rclcpp::shutdown();
  if (!success) {
    std::cerr << "fault-control command failed: " << command << '\n';
    return 1;
  }
  std::cout << "fault-control command passed: " << command << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return Run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "nav2 fault control failed: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
