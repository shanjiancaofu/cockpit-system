#include "cockpit/library/bridge/ros2_camera_info_adapter.h"

#include <algorithm>
#include <stdexcept>

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
  if (source.d.size() != 5 || source.k.size() != 9 || source.r.size() != 9 ||
      source.p.size() != 12) {
    throw std::invalid_argument("CameraInfo arrays must have D=5, K=9, R=9, P=12 elements");
  }
  result.d = source.d;
  std::copy(source.k.begin(), source.k.end(), result.k.begin());
  std::copy(source.r.begin(), source.r.end(), result.r.begin());
  std::copy(source.p.begin(), source.p.end(), result.p.begin());
  return result;
}

}  // namespace cockpit::bridge
