#include "cockpit/library/bridge/ros2_camera_info_adapter.h"

#include <algorithm>

namespace cockpit::bridge {

sensor_msgs::msg::CameraInfo ToRosCameraInfo(const hawkeye::CameraInfo& source,
                                             const std::string& frame_id,
                                             const builtin_interfaces::msg::Time& stamp) {
  sensor_msgs::msg::CameraInfo result;
  result.header.frame_id = frame_id;
  result.header.stamp = stamp;
  result.width = source.width;
  result.height = source.height;
  result.distortion_model = source.distortion_model;
  result.d = source.d;
  std::copy_n(source.k.begin(), std::min(source.k.size(), result.k.size()), result.k.begin());
  std::copy_n(source.r.begin(), std::min(source.r.size(), result.r.size()), result.r.begin());
  std::copy_n(source.p.begin(), std::min(source.p.size(), result.p.size()), result.p.begin());
  return result;
}

}  // namespace cockpit::bridge
