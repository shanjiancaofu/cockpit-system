#pragma once

#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>

#include "cockpit/library/bridge/ros2_camera_frame_adapter.h"

namespace cockpit::bridge {

struct Ros2CameraTopics {
  std::string image_raw = "/camera/image_raw";
  std::string camera_info = "/camera/camera_info";
  std::string image_rect = "/camera/image_rect";
};

// Reusable publisher owned by a Camera runtime/node. It does not open a camera
// and cannot bypass the existing CameraFrame producer.
class Ros2CameraPublisher final {
 public:
  Ros2CameraPublisher(rclcpp::Node* node, hawkeye::CameraInfo camera_info, std::string frame_id,
                      bool enable_rectify, Ros2CameraTopics topics = {});

  bool Publish(const camera::CameraFrame& frame, std::string* error = nullptr);

 private:
  Ros2CameraFrameAdapter adapter_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_raw_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_rect_publisher_;
};

}  // namespace cockpit::bridge
