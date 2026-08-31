#pragma once

#include <array>
#include <cstdint>

#include "cockpit/modules/hawkeye/camera_calibration.h"

namespace cockpit::hawkeye {

struct LidarToCameraExtrinsic {
  std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  std::array<double, 3> translation_m{};
};

struct ProjectedLidarPixel {
  double u = 0.0;
  double v = 0.0;
  double camera_depth_m = 0.0;
};

enum class LidarCameraProjectionStatus {
  kProjected,
  kInvalidInput,
  kTimestampMismatch,
  kBehindCamera,
  kOutsideImage,
};

// Projects one planar LaserScan sample into a rectified image. The supplied
// extrinsic maps LiDAR Cartesian coordinates into the camera optical frame.
LidarCameraProjectionStatus ProjectLidarPointToRectifiedImage(
    double range_m, double angle_rad, std::int64_t lidar_sample_timestamp_ns,
    std::int64_t camera_sample_timestamp_ns, std::int64_t max_timestamp_delta_ns,
    const LidarToCameraExtrinsic& lidar_to_camera, const CameraCalibration& camera_calibration,
    ProjectedLidarPixel* output);

}  // namespace cockpit::hawkeye
