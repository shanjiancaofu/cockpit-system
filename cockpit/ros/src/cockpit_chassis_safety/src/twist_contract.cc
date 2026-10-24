#include "cockpit_chassis_safety/twist_contract.h"

#include <cmath>

namespace cockpit::chassis_safety {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

}  // namespace

bool ToDifferentialDriveRequest(const geometry_msgs::msg::Twist& message,
                                vehicle::ChassisVelocityRequest* request, std::string* error) {
  if (request == nullptr) {
    AssignError(error, "velocity request output must not be null");
    return false;
  }
  const double components[] = {message.linear.x,  message.linear.y,  message.linear.z,
                               message.angular.x, message.angular.y, message.angular.z};
  for (const double component : components) {
    if (!std::isfinite(component)) {
      AssignError(error, "Twist components must all be finite");
      return false;
    }
  }
  if (message.linear.y != 0.0 || message.linear.z != 0.0 || message.angular.x != 0.0 ||
      message.angular.y != 0.0) {
    AssignError(error, "differential chassis only supports linear.x and angular.z");
    return false;
  }
  request->linear_velocity_m_s = message.linear.x;
  request->angular_velocity_rad_s = message.angular.z;
  return true;
}

}  // namespace cockpit::chassis_safety
