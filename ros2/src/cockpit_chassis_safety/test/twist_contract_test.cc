#include "cockpit_chassis_safety/twist_contract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  cockpit::vehicle::ChassisVelocityRequest request;
  std::string error;
  geometry_msgs::msg::Twist message;
  message.linear.x = 0.2;
  message.angular.z = -0.4;
  Require(cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "valid vx/wz Twist was rejected");
  Require(std::abs(request.linear_velocity_m_s - 0.2) < 1.0e-12 &&
              std::abs(request.angular_velocity_rad_s + 0.4) < 1.0e-12,
          "valid Twist conversion mismatch");

  message.linear.y = 0.1;
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "unsupported linear.y was accepted");
  message.linear.y = 0.0;
  message.linear.z = -0.1;
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "unsupported linear.z was accepted");
  message.linear.z = 0.0;
  message.angular.x = 0.1;
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "unsupported angular.x was accepted");
  message.angular.x = 0.0;
  message.angular.y = -0.1;
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "unsupported angular.y was accepted");
  message.angular.y = 0.0;

  const double nan = std::numeric_limits<double>::quiet_NaN();
  message.linear.y = nan;
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "NaN unsupported axis was accepted");
  message.linear.y = 0.0;
  message.angular.z = std::numeric_limits<double>::infinity();
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, &request, &error),
          "infinite supported axis was accepted");
  Require(!cockpit::chassis_safety::ToDifferentialDriveRequest(message, nullptr, &error),
          "null request output was accepted");

  std::cout << "differential Twist contract tests passed\n";
  return 0;
}
