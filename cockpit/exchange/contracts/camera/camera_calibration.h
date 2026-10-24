#pragma once

#include <cstdint>

namespace cockpit::hawkeye {

enum class CameraDistortionModel {
  kPlumbBob,
};

struct CameraCalibration {
  std::uint32_t image_width = 0;
  std::uint32_t image_height = 0;
  double fx = 0.0;
  double fy = 0.0;
  double cx = 0.0;
  double cy = 0.0;
  CameraDistortionModel distortion_model = CameraDistortionModel::kPlumbBob;
  double k1 = 0.0;
  double k2 = 0.0;
  double p1 = 0.0;
  double p2 = 0.0;
  double k3 = 0.0;
};

const char* CameraDistortionModelName(CameraDistortionModel model);

}  // namespace cockpit::hawkeye
