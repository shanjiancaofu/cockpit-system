#pragma once

#include <builtin_interfaces/msg/time.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <string>

#include "cockpit/modules/hawkeye/camera_info.h"

namespace cockpit::bridge {

sensor_msgs::msg::CameraInfo ToRosCameraInfo(const hawkeye::CameraInfo& source,
                                             const std::string& frame_id,
                                             const builtin_interfaces::msg::Time& stamp);

}  // namespace cockpit::bridge
