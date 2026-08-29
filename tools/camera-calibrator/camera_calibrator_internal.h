#pragma once

#include <cstdint>
#include <filesystem>
#include <opencv2/core.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cockpit/modules/camera/frames/camera_frame.h"

namespace cockpit::camera_calibrator {

struct Options {
  std::string device = "nvargus://0";
  std::filesystem::path input_dir;
  std::filesystem::path output_dir = "_output/runtime/camera-calibration";
  std::string board_profile;
  int width = 1920;
  int height = 1080;
  int fps = 30;
  int frames = 30;
  int timeout_seconds = 120;
  int corners_x = 9;
  int corners_y = 6;
  double square_size = 0.025;
  double blur_min = 80.0;
  double mean_min = 15.0;
  double mean_max = 245.0;
  double area_min = 0.02;
  double area_max = 0.80;
  int grid_required = 5;
  double duplicate_threshold = 2.0;
  bool corners_x_explicit = false;
  bool corners_y_explicit = false;
  bool square_size_explicit = false;
};

enum class ParseResult { kOk, kHelp, kError };

struct AcceptedFrame {
  cv::Mat image;
  std::vector<cv::Point2f> corners;
  double blur = 0.0;
  double mean = 0.0;
  double area = 0.0;
  int grid = 0;
  std::uint64_t sequence = 0;
  double center_x_normalized = 0.0;
  double center_y_normalized = 0.0;
  bool pose_valid = false;
  double yaw_deg = 0.0;
  double pitch_deg = 0.0;
  double roll_deg = 0.0;
  double distance_m = 0.0;
  double reprojection_error_px = 0.0;
  bool selected = true;
  bool outlier = false;
};

void Usage();
ParseResult ParseOptions(int argc, char** argv, Options* options);
cv::Mat ToBgr(const cockpit::camera::CameraFrame& frame);
std::optional<AcceptedFrame> Analyze(const cv::Mat& image, std::uint64_t sequence,
                                     const Options& options,
                                     const std::vector<AcceptedFrame>& accepted);
std::vector<std::filesystem::path> ImageFiles(const std::filesystem::path& directory);
bool Calibrate(const Options& options, std::vector<AcceptedFrame> accepted);

}  // namespace cockpit::camera_calibrator
