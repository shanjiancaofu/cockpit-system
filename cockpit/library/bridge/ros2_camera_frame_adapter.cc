#include "cockpit/library/bridge/ros2_camera_frame_adapter.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

#include "cockpit/library/bridge/ros2_camera_info_adapter.h"

namespace cockpit::bridge {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

std::int64_t FrameTimestampNs(const camera::CameraFrame& frame) {
  if (frame.source_timestamp_valid &&
      frame.source_clock == camera::CameraTimestampClock::kRealtime &&
      frame.source_timestamp_ns > 0) {
    return frame.source_timestamp_ns;
  }
  if (frame.received_at_ns > 0) return frame.received_at_ns;
  return static_cast<std::int64_t>(frame.timestamp_ms) * 1000000LL;
}

}  // namespace

builtin_interfaces::msg::Time CameraFrameSourceStamp(const camera::CameraFrame& frame) {
  const std::int64_t timestamp_ns = std::max<std::int64_t>(0, FrameTimestampNs(frame));
  builtin_interfaces::msg::Time stamp;
  stamp.sec = static_cast<std::int32_t>(timestamp_ns / 1000000000LL);
  stamp.nanosec = static_cast<std::uint32_t>(timestamp_ns % 1000000000LL);
  return stamp;
}

Ros2CameraFrameAdapter::Ros2CameraFrameAdapter(hawkeye::CameraInfo camera_info,
                                               std::string frame_id, bool enable_rectify)
    : camera_info_(std::move(camera_info)),
      frame_id_(std::move(frame_id)),
      enable_rectify_(enable_rectify) {
}

bool Ros2CameraFrameAdapter::InitializeRectify(std::string* error) const {
  if (maps_initialized_) return true;
  if (camera_info_.width == 0 || camera_info_.height == 0 || camera_info_.k.size() != 9 ||
      camera_info_.d.size() != 5) {
    AssignError(error, "invalid CameraInfo for rectification");
    return false;
  }
  cv::Mat camera_matrix(3, 3, CV_64F);
  cv::Mat distortion(1, 5, CV_64F);
  std::copy(camera_info_.k.begin(), camera_info_.k.end(), camera_matrix.ptr<double>());
  std::copy(camera_info_.d.begin(), camera_info_.d.end(), distortion.ptr<double>());
  cv::Mat identity = cv::Mat::eye(3, 3, CV_64F);
  cv::initUndistortRectifyMap(
      camera_matrix, distortion, identity, camera_matrix,
      cv::Size(static_cast<int>(camera_info_.width), static_cast<int>(camera_info_.height)),
      CV_32FC1, map1_, map2_);
  maps_initialized_ = true;
  return true;
}

bool Ros2CameraFrameAdapter::Convert(const camera::CameraFrame& frame,
                                     Ros2CameraFrameOutput* output, std::string* error) const {
  if (output == nullptr || frame.format != camera::CameraPixelFormat::kBgrx ||
      frame.width != camera_info_.width || frame.height != camera_info_.height ||
      frame.stride_bytes < frame.width * 4U ||
      frame.data.size() < static_cast<std::size_t>(frame.stride_bytes) * frame.height) {
    AssignError(error, "CameraFrame does not match verified CameraInfo layout");
    return false;
  }
  if (enable_rectify_ && !InitializeRectify(error)) return false;

  const auto stamp = CameraFrameSourceStamp(frame);
  output->camera_info = ToRosCameraInfo(camera_info_, frame_id_, stamp);
  output->image_raw = sensor_msgs::msg::Image{};
  output->image_raw.header.frame_id = frame_id_;
  output->image_raw.header.stamp = stamp;
  output->image_raw.height = frame.height;
  output->image_raw.width = frame.width;
  output->image_raw.encoding = "bgr8";
  output->image_raw.is_bigendian = false;
  output->image_raw.step = frame.width * 3U;
  output->image_raw.data.resize(static_cast<std::size_t>(output->image_raw.step) * frame.height);
  cv::Mat source(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC4,
                 const_cast<std::uint8_t*>(frame.data.data()), frame.stride_bytes);
  cv::Mat bgr(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC3,
              output->image_raw.data.data(), output->image_raw.step);
  cv::cvtColor(source, bgr, cv::COLOR_BGRA2BGR);

  output->has_rectified_image = false;
  if (enable_rectify_) {
    output->image_rect = output->image_raw;
    output->image_rect.data.resize(output->image_raw.data.size());
    cv::Mat rectified(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC3,
                      output->image_rect.data.data(), output->image_rect.step);
    cv::Mat raw_image(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC3,
                      output->image_raw.data.data(), output->image_raw.step);
    cv::remap(raw_image, rectified, map1_, map2_, cv::INTER_LINEAR);
    output->image_rect.header = output->image_raw.header;
    output->has_rectified_image = true;
  }
  return true;
}

}  // namespace cockpit::bridge
