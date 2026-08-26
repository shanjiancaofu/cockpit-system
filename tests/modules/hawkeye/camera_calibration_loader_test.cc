#include "cockpit/modules/hawkeye/camera_calibration_loader.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "cockpit/modules/hawkeye/camera_info.h"

namespace {

constexpr char kValidCalibration[] = R"yaml(image_width: 1280
image_height: 720
fx: 800.5
fy: 801.5
cx: 640.0
cy: 360.0
distortion_model: plumb_bob
k1: -0.1
k2: 0.01
p1: 0.001
p2: -0.002
k3: 0.0001
)yaml";

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string ReplaceOnce(std::string text, const std::string& from, const std::string& to) {
  const std::size_t position = text.find(from);
  Require(position != std::string::npos, "test fixture replacement source is missing: " + from);
  text.replace(position, from.size(), to);
  return text;
}

class FixtureFile final {
 public:
  explicit FixtureFile(const std::string& content)
      : path_(std::filesystem::temp_directory_path() /
              ("cockpit-camera-calibration-" + std::to_string(getpid()) + "-" +
               std::to_string(next_id_++) + ".yaml")) {
    std::ofstream output(path_);
    output << content;
    Require(output.good(), "failed to write camera calibration fixture");
  }

  ~FixtureFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path& path() const {
    return path_;
  }

 private:
  static int next_id_;
  std::filesystem::path path_;
};

int FixtureFile::next_id_ = 0;

void ExpectRejected(const std::string& content, const std::string& expected_error) {
  FixtureFile fixture(content);
  cockpit::hawkeye::CameraCalibration calibration;
  std::string error;
  Require(!cockpit::hawkeye::CameraCalibrationLoader::LoadFromFile(fixture.path().string(),
                                                                   &calibration, &error),
          "invalid camera calibration was accepted");
  Require(error.find(expected_error) != std::string::npos,
          "camera calibration error did not contain '" + expected_error + "': " + error);
}

}  // namespace

int main() {
  FixtureFile valid(kValidCalibration);
  cockpit::hawkeye::CameraCalibration calibration;
  std::string error;
  Require(cockpit::hawkeye::CameraCalibrationLoader::LoadFromFile(valid.path().string(),
                                                                  &calibration, &error),
          "valid camera calibration was rejected: " + error);
  Require(calibration.image_width == 1280 && calibration.image_height == 720 &&
              calibration.fx == 800.5 && calibration.fy == 801.5 && calibration.cx == 640.0 &&
              calibration.cy == 360.0 && calibration.k1 == -0.1 && calibration.k2 == 0.01 &&
              calibration.p1 == 0.001 && calibration.p2 == -0.002 && calibration.k3 == 0.0001 &&
              std::string(cockpit::hawkeye::CameraDistortionModelName(
                  calibration.distortion_model)) == "plumb_bob",
          "valid camera calibration values were not preserved");
  cockpit::hawkeye::CameraInfo camera_info;
  Require(cockpit::hawkeye::ToCameraInfo(calibration, &camera_info, &error),
          "CameraInfo conversion failed: " + error);
  Require(camera_info.width == 1280 && camera_info.height == 720 &&
              camera_info.distortion_model == "plumb_bob" && camera_info.d.size() == 5 &&
              camera_info.k.size() == 9 && camera_info.r.size() == 9 &&
              camera_info.p.size() == 12 && camera_info.k[0] == 800.5 &&
              camera_info.k[2] == 640.0 && camera_info.p[6] == 360.0,
          "CameraInfo conversion values are invalid");

  ExpectRejected(ReplaceOnce(kValidCalibration, "fx: 800.5\n", ""),
                 "camera_calibration.fx is required");
  ExpectRejected(ReplaceOnce(kValidCalibration, "image_width: 1280", "image_width: 0"),
                 "image_width must be a positive uint32 value");
  ExpectRejected(ReplaceOnce(kValidCalibration, "image_height: 720", "image_height: -1"),
                 "image_height must be a positive uint32 value");
  ExpectRejected(ReplaceOnce(kValidCalibration, "fx: 800.5", "fx: 0"),
                 "camera_calibration.fx must be greater than zero");
  ExpectRejected(ReplaceOnce(kValidCalibration, "fy: 801.5", "fy: -1"),
                 "camera_calibration.fy must be greater than zero");
  ExpectRejected(ReplaceOnce(kValidCalibration, "cx: 640.0", "cx: .nan"),
                 "camera_calibration.cx must be finite");
  ExpectRejected(ReplaceOnce(kValidCalibration, "k1: -0.1", "k1: .inf"),
                 "camera_calibration.k1 must be finite");
  ExpectRejected(ReplaceOnce(kValidCalibration, "plumb_bob", "rational_polynomial"),
                 "distortion_model is not supported");
  ExpectRejected(std::string(kValidCalibration) + "vendor_extension: 1\n",
                 "camera_calibration.vendor_extension is not supported");
  ExpectRejected(std::string(kValidCalibration) + "fx: 900.0\n",
                 "camera_calibration.fx is duplicated");
  ExpectRejected("image_width: [1280\n", "failed to load camera calibration file");

  cockpit::hawkeye::CameraCalibration missing;
  error.clear();
  Require(!cockpit::hawkeye::CameraCalibrationLoader::LoadFromFile(
              "/tmp/cockpit-camera-calibration-does-not-exist.yaml", &missing, &error) &&
              error.find("failed to load camera calibration file") != std::string::npos,
          "missing camera calibration did not return a clear error");

  std::cout << "camera calibration loader tests passed\n";
  return 0;
}
