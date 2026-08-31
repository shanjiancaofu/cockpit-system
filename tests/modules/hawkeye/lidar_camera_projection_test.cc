#include "cockpit/modules/hawkeye/lidar_camera_projection.h"

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
  using cockpit::hawkeye::LidarCameraProjectionStatus;
  cockpit::hawkeye::CameraCalibration calibration;
  calibration.image_width = 640;
  calibration.image_height = 480;
  calibration.fx = 100.0;
  calibration.fy = 100.0;
  calibration.cx = 320.0;
  calibration.cy = 240.0;
  cockpit::hawkeye::LidarToCameraExtrinsic extrinsic;
  extrinsic.translation_m[2] = 2.0;
  cockpit::hawkeye::ProjectedLidarPixel pixel;
  Require(cockpit::hawkeye::ProjectLidarPointToRectifiedImage(
              1.0, 0.0, 1000000000LL, 1010000000LL, 20000000LL, extrinsic, calibration, &pixel) ==
                  LidarCameraProjectionStatus::kProjected &&
              std::abs(pixel.u - 370.0) < 1e-9 && std::abs(pixel.v - 240.0) < 1e-9 &&
              std::abs(pixel.camera_depth_m - 2.0) < 1e-9,
          "valid LaserScan point was not projected into the rectified image");

  auto behind = extrinsic;
  behind.translation_m[2] = -1.0;
  Require(cockpit::hawkeye::ProjectLidarPointToRectifiedImage(
              1.0, 0.0, 1000000000LL, 1000000000LL, 20000000LL, behind, calibration, &pixel) ==
              LidarCameraProjectionStatus::kBehindCamera,
          "point behind camera was accepted");
  Require(cockpit::hawkeye::ProjectLidarPointToRectifiedImage(
              10.0, 0.0, 1000000000LL, 1000000000LL, 20000000LL, extrinsic, calibration, &pixel) ==
              LidarCameraProjectionStatus::kOutsideImage,
          "point outside image was accepted");
  Require(cockpit::hawkeye::ProjectLidarPointToRectifiedImage(
              std::numeric_limits<double>::quiet_NaN(), 0.0, 1000000000LL, 1000000000LL, 20000000LL,
              extrinsic, calibration, &pixel) == LidarCameraProjectionStatus::kInvalidInput,
          "NaN LaserScan range was accepted");
  Require(cockpit::hawkeye::ProjectLidarPointToRectifiedImage(
              1.0, 0.0, 1000000000LL, 1100000000LL, 20000000LL, extrinsic, calibration, &pixel) ==
              LidarCameraProjectionStatus::kTimestampMismatch,
          "timestamp-mismatched point was accepted");
  extrinsic.rotation[0] = std::numeric_limits<double>::infinity();
  Require(cockpit::hawkeye::ProjectLidarPointToRectifiedImage(
              1.0, 0.0, 1000000000LL, 1000000000LL, 20000000LL, extrinsic, calibration, &pixel) ==
              LidarCameraProjectionStatus::kInvalidInput,
          "invalid extrinsic was accepted");

  std::cout << "LiDAR to rectified Camera projection tests passed\n";
  return 0;
}
