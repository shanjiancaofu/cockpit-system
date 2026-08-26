#pragma once

#include <chrono>
#include <memory>
#include <rclcpp/context.hpp>
#include <string>

#include "cockpit/modules/bridge/bridge_provider.h"

namespace cockpit::bridge {

struct Ros2Nav2ProviderOptions {
  std::string action_name = "/navigate_to_pose";
  std::chrono::milliseconds server_timeout{1000};
  rclcpp::Context::SharedPtr context;
};

std::unique_ptr<NavigationProvider> CreateRos2Nav2Provider(const Ros2Nav2ProviderOptions& options,
                                                           std::string* error);

}  // namespace cockpit::bridge
