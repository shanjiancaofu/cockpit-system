#pragma once

#include <builtin_interfaces/msg/time.hpp>
#include <opencv2/core.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>

#include "cockpit/modules/camera/frames/camera_frame.h"
#include "cockpit/modules/hawkeye/camera_info.h"

namespace cockpit::bridge {

struct Ros2CameraFrameOutput {
  sensor_msgs::msg::Image image_raw;
  sensor_msgs::msg::CameraInfo camera_info;
  sensor_msgs::msg::Image image_rect;
  bool has_rectified_image = false;
};

class Ros2CameraFrameAdapter final {
 public:
  Ros2CameraFrameAdapter(hawkeye::CameraInfo camera_info, std::string frame_id,
                         bool enable_rectify);

  bool Convert(const camera::CameraFrame& frame, Ros2CameraFrameOutput* output,
               std::string* error = nullptr) const;

 private:
  bool InitializeRectify(std::string* error) const;

  hawkeye::CameraInfo camera_info_;
  std::string frame_id_;
  bool enable_rectify_ = false;
  mutable bool maps_initialized_ = false;
  mutable cv::Mat map1_;
  mutable cv::Mat map2_;
};

builtin_interfaces::msg::Time CameraFrameSourceStamp(const camera::CameraFrame& frame);

}  // namespace cockpit::bridge
