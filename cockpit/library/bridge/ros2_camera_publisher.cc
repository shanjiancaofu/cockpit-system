#include "cockpit/library/bridge/ros2_camera_publisher.h"

#include <stdexcept>
#include <utility>

namespace cockpit::bridge {

Ros2CameraPublisher::Ros2CameraPublisher(rclcpp::Node* node, hawkeye::CameraInfo camera_info,
                                         std::string frame_id, bool enable_rectify,
                                         Ros2CameraTopics topics)
    : adapter_(std::move(camera_info), std::move(frame_id), enable_rectify) {
  if (node == nullptr || topics.image_raw.empty() || topics.camera_info.empty() ||
      (enable_rectify && topics.image_rect.empty()) || topics.image_raw == topics.camera_info ||
      (enable_rectify &&
       (topics.image_raw == topics.image_rect || topics.camera_info == topics.image_rect))) {
    throw std::invalid_argument("invalid ROS2 camera publisher configuration");
  }
  const auto qos = rclcpp::SensorDataQoS();
  image_raw_publisher_ = node->create_publisher<sensor_msgs::msg::Image>(topics.image_raw, qos);
  camera_info_publisher_ =
      node->create_publisher<sensor_msgs::msg::CameraInfo>(topics.camera_info, qos);
  if (enable_rectify) {
    image_rect_publisher_ = node->create_publisher<sensor_msgs::msg::Image>(topics.image_rect, qos);
  }
}

bool Ros2CameraPublisher::Publish(const camera::CameraFrame& frame, std::string* error) {
  Ros2CameraFrameOutput output;
  if (!adapter_.Convert(frame, &output, error)) return false;
  image_raw_publisher_->publish(std::move(output.image_raw));
  camera_info_publisher_->publish(std::move(output.camera_info));
  if (output.has_rectified_image && image_rect_publisher_ != nullptr) {
    image_rect_publisher_->publish(std::move(output.image_rect));
  }
  return true;
}

}  // namespace cockpit::bridge
