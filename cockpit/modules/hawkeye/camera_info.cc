#include "cockpit/modules/hawkeye/camera_info.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cockpit::hawkeye {

bool ToCameraInfo(const CameraCalibration& calibration, CameraInfo* info, std::string* error) {
  if (info == nullptr || error == nullptr) {
    return false;
  }
  error->clear();
  const std::array<double, 9> parameters{calibration.fx, calibration.fy, calibration.cx,
                                         calibration.cy, calibration.k1, calibration.k2,
                                         calibration.p1, calibration.p2, calibration.k3};
  const bool finite_parameters =
      std::all_of(parameters.begin(), parameters.end(), [](double value) {
        return std::isfinite(value);
      });
  if (calibration.image_width == 0 || calibration.image_height == 0 || !finite_parameters ||
      calibration.fx <= 0.0 || calibration.fy <= 0.0) {
    *error = "camera calibration is invalid for CameraInfo conversion";
    return false;
  }
  if (calibration.distortion_model != CameraDistortionModel::kPlumbBob) {
    *error = "camera distortion model is unsupported for CameraInfo conversion";
    return false;
  }
  *info = CameraInfo{};
  info->width = calibration.image_width;
  info->height = calibration.image_height;
  info->distortion_model = "plumb_bob";
  info->d = {calibration.k1, calibration.k2, calibration.p1, calibration.p2, calibration.k3};
  info->k = {
      calibration.fx, 0.0, calibration.cx, 0.0, calibration.fy, calibration.cy, 0.0, 0.0, 1.0};
  info->r = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  info->p = {calibration.fx,
             0.0,
             calibration.cx,
             0.0,
             0.0,
             calibration.fy,
             calibration.cy,
             0.0,
             0.0,
             0.0,
             1.0,
             0.0};
  return true;
}

}  // namespace cockpit::hawkeye
