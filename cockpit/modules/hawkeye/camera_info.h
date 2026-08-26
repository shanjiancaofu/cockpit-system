#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cockpit/modules/hawkeye/camera_calibration.h"

namespace cockpit::hawkeye {

struct CameraInfo {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::string distortion_model;
  std::vector<double> d;
  std::vector<double> k;
  std::vector<double> r;
  std::vector<double> p;
};

bool ToCameraInfo(const CameraCalibration& calibration, CameraInfo* info, std::string* error);

}  // namespace cockpit::hawkeye
