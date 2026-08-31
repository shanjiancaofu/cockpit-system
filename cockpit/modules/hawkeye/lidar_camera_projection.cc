#include "cockpit/modules/hawkeye/lidar_camera_projection.h"

#include <cmath>

namespace cockpit::hawkeye {
namespace {

bool CalibrationIsValid(const CameraCalibration& calibration) {
  return calibration.image_width > 0 && calibration.image_height > 0 &&
         std::isfinite(calibration.fx) && calibration.fx > 0.0 && std::isfinite(calibration.fy) &&
         calibration.fy > 0.0 && std::isfinite(calibration.cx) && std::isfinite(calibration.cy);
}

bool ExtrinsicIsFinite(const LidarToCameraExtrinsic& extrinsic) {
  for (double value : extrinsic.rotation) {
    if (!std::isfinite(value)) return false;
  }
  for (double value : extrinsic.translation_m) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

std::uint64_t TimestampDelta(std::int64_t first, std::int64_t second) {
  return first >= second ? static_cast<std::uint64_t>(first - second)
                         : static_cast<std::uint64_t>(second - first);
}

}  // namespace

LidarCameraProjectionStatus ProjectLidarPointToRectifiedImage(
    double range_m, double angle_rad, std::int64_t lidar_sample_timestamp_ns,
    std::int64_t camera_sample_timestamp_ns, std::int64_t max_timestamp_delta_ns,
    const LidarToCameraExtrinsic& lidar_to_camera, const CameraCalibration& camera_calibration,
    ProjectedLidarPixel* output) {
  if (output == nullptr || !std::isfinite(range_m) || range_m <= 0.0 || !std::isfinite(angle_rad) ||
      lidar_sample_timestamp_ns <= 0 || camera_sample_timestamp_ns <= 0 ||
      max_timestamp_delta_ns < 0 || !ExtrinsicIsFinite(lidar_to_camera) ||
      !CalibrationIsValid(camera_calibration)) {
    return LidarCameraProjectionStatus::kInvalidInput;
  }
  if (TimestampDelta(lidar_sample_timestamp_ns, camera_sample_timestamp_ns) >
      static_cast<std::uint64_t>(max_timestamp_delta_ns)) {
    return LidarCameraProjectionStatus::kTimestampMismatch;
  }

  const std::array<double, 3> lidar_point{range_m * std::cos(angle_rad),
                                          range_m * std::sin(angle_rad), 0.0};
  std::array<double, 3> camera_point{};
  for (std::size_t row = 0; row < 3; ++row) {
    camera_point[row] = lidar_to_camera.translation_m[row];
    for (std::size_t column = 0; column < 3; ++column) {
      camera_point[row] += lidar_to_camera.rotation[row * 3 + column] * lidar_point[column];
    }
  }
  if (!std::isfinite(camera_point[0]) || !std::isfinite(camera_point[1]) ||
      !std::isfinite(camera_point[2])) {
    return LidarCameraProjectionStatus::kInvalidInput;
  }
  if (camera_point[2] <= 0.0) return LidarCameraProjectionStatus::kBehindCamera;

  const double u =
      camera_calibration.fx * camera_point[0] / camera_point[2] + camera_calibration.cx;
  const double v =
      camera_calibration.fy * camera_point[1] / camera_point[2] + camera_calibration.cy;
  if (!std::isfinite(u) || !std::isfinite(v)) {
    return LidarCameraProjectionStatus::kInvalidInput;
  }
  if (u < 0.0 || v < 0.0 || u >= static_cast<double>(camera_calibration.image_width) ||
      v >= static_cast<double>(camera_calibration.image_height)) {
    return LidarCameraProjectionStatus::kOutsideImage;
  }
  *output = ProjectedLidarPixel{u, v, camera_point[2]};
  return LidarCameraProjectionStatus::kProjected;
}

}  // namespace cockpit::hawkeye
