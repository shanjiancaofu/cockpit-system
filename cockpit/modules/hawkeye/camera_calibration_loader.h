#pragma once

#include <string>

#include "cockpit/modules/hawkeye/camera_calibration.h"

namespace cockpit::hawkeye {

class CameraCalibrationLoader final {
 public:
  static bool LoadFromFile(const std::string& path, CameraCalibration* calibration,
                           std::string* error);
};

}  // namespace cockpit::hawkeye
