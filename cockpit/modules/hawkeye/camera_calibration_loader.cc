#include "cockpit/modules/hawkeye/camera_calibration_loader.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace cockpit::hawkeye {
namespace {

constexpr std::string_view kRootPath = "camera_calibration";
const std::vector<std::string_view> kAllowedKeys = {
    "image_width",      "image_height", "fx", "fy", "cx", "cy",
    "distortion_model", "k1",           "k2", "p1", "p2", "k3",
};

bool Fail(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool ReadRequiredScalar(const YAML::Node& root, const char* key, YAML::Node* result,
                        std::string* error) {
  const YAML::Node value = root[key];
  const std::string path = std::string(kRootPath) + "." + key;
  if (!value.IsDefined()) {
    return Fail(error, path + " is required");
  }
  if (!value.IsScalar()) {
    return Fail(error, path + " must be a scalar");
  }
  *result = value;
  return true;
}

bool ReadDimension(const YAML::Node& root, const char* key, std::uint32_t* result,
                   std::string* error) {
  YAML::Node value;
  if (!ReadRequiredScalar(root, key, &value, error)) {
    return false;
  }
  const std::string path = std::string(kRootPath) + "." + key;
  try {
    const std::int64_t parsed = value.as<std::int64_t>();
    if (parsed <= 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
      return Fail(error, path + " must be a positive uint32 value");
    }
    *result = static_cast<std::uint32_t>(parsed);
    return true;
  } catch (const YAML::Exception& exception) {
    return Fail(error, path + " has invalid integer value: " + exception.msg);
  }
}

bool ReadFinite(const YAML::Node& root, const char* key, double* result, std::string* error) {
  YAML::Node value;
  if (!ReadRequiredScalar(root, key, &value, error)) {
    return false;
  }
  const std::string path = std::string(kRootPath) + "." + key;
  try {
    const double parsed = value.as<double>();
    if (!std::isfinite(parsed)) {
      return Fail(error, path + " must be finite");
    }
    *result = parsed;
    return true;
  } catch (const YAML::Exception& exception) {
    return Fail(error, path + " has invalid floating-point value: " + exception.msg);
  }
}

bool ReadDistortionModel(const YAML::Node& root, CameraDistortionModel* model, std::string* error) {
  YAML::Node value;
  if (!ReadRequiredScalar(root, "distortion_model", &value, error)) {
    return false;
  }
  try {
    const std::string name = value.as<std::string>();
    if (name != "plumb_bob") {
      return Fail(error, "camera_calibration.distortion_model is not supported: " + name);
    }
    *model = CameraDistortionModel::kPlumbBob;
    return true;
  } catch (const YAML::Exception& exception) {
    return Fail(error, "camera_calibration.distortion_model has invalid value: " + exception.msg);
  }
}

}  // namespace

bool CameraCalibrationLoader::LoadFromFile(const std::string& path, CameraCalibration* calibration,
                                           std::string* error) {
  if (calibration == nullptr || error == nullptr) {
    return false;
  }
  error->clear();
  if (path.empty()) {
    return Fail(error, "camera calibration path must not be empty");
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception& exception) {
    return Fail(error, "failed to load camera calibration file " + path + ": " + exception.msg);
  }
  if (!root.IsMap()) {
    return Fail(error, "camera calibration root must be a map: " + path);
  }
  for (const auto& item : root) {
    if (!item.first.IsScalar()) {
      return Fail(error, "camera calibration keys must be scalars");
    }
    const std::string key = item.first.as<std::string>();
    if (std::find(kAllowedKeys.begin(), kAllowedKeys.end(), std::string_view(key)) ==
        kAllowedKeys.end()) {
      return Fail(error, std::string(kRootPath) + "." + key + " is not supported");
    }
  }

  CameraCalibration parsed;
  if (!ReadDimension(root, "image_width", &parsed.image_width, error) ||
      !ReadDimension(root, "image_height", &parsed.image_height, error) ||
      !ReadFinite(root, "fx", &parsed.fx, error) || !ReadFinite(root, "fy", &parsed.fy, error) ||
      !ReadFinite(root, "cx", &parsed.cx, error) || !ReadFinite(root, "cy", &parsed.cy, error) ||
      !ReadDistortionModel(root, &parsed.distortion_model, error) ||
      !ReadFinite(root, "k1", &parsed.k1, error) || !ReadFinite(root, "k2", &parsed.k2, error) ||
      !ReadFinite(root, "p1", &parsed.p1, error) || !ReadFinite(root, "p2", &parsed.p2, error) ||
      !ReadFinite(root, "k3", &parsed.k3, error)) {
    return false;
  }
  if (parsed.fx <= 0.0) {
    return Fail(error, "camera_calibration.fx must be greater than zero");
  }
  if (parsed.fy <= 0.0) {
    return Fail(error, "camera_calibration.fy must be greater than zero");
  }

  *calibration = parsed;
  return true;
}

}  // namespace cockpit::hawkeye
