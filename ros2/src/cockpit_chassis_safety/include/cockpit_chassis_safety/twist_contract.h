#pragma once

#include <geometry_msgs/msg/twist.hpp>
#include <string>

#include "cockpit/modules/vehicle/chassis_safety_adapter.h"

namespace cockpit::chassis_safety {

bool ToDifferentialDriveRequest(const geometry_msgs::msg::Twist& message,
                                vehicle::ChassisVelocityRequest* request,
                                std::string* error = nullptr);

}  // namespace cockpit::chassis_safety
