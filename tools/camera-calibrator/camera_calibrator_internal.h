#pragma once

#include <array>
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
  std::vector<std::filesystem::path> input_videos;
  std::filesystem::path output_dir = "_output/runtime/camera-calibration";
  std::string board_profile;
  int width = 1920;
  int height = 1080;
  int fps = 30;
  int frames = 30;
  int max_candidates = 50;
  int timeout_seconds = 120;
  int corners_x = 9;
  int corners_y = 6;
  double square_size = 0.025;
  double blur_min = 80.0;
  double mean_min = 15.0;
  double mean_max = 245.0;
  double area_min = 0.02;
  double area_max = 0.80;
  double near_distance_m = 0.25;
  double far_distance_m = 0.55;
  double tilt_threshold_deg = 12.0;
  int grid_required = 5;
  double duplicate_threshold = 0.0;
  bool corners_x_explicit = false;
  bool corners_y_explicit = false;
  bool square_size_explicit = false;
  bool blur_min_explicit = false;
  bool area_min_explicit = false;
  bool area_max_explicit = false;
  bool grid_required_explicit = false;
  bool duplicate_threshold_explicit = false;
  bool near_distance_explicit = false;
  bool far_distance_explicit = false;
  bool tilt_threshold_explicit = false;
  bool preview = false;
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
  std::string source_video;
  std::uint64_t source_frame_index = 0;
  std::int64_t source_timestamp_ms = 0;
  double center_x_normalized = 0.0;
  double center_y_normalized = 0.0;
  bool pose_valid = false;
  double horizontal_tilt_deg = 0.0;
  double vertical_tilt_deg = 0.0;
  double in_plane_rotation_deg = 0.0;
  double distance_m = 0.0;
  double reprojection_error_px = 0.0;
  bool selected = true;
  bool outlier = false;
};

enum class RejectReason {
  kAccepted,
  kNoChessboard,
  kBlur,
  kExposure,
  kAreaTooSmall,
  kAreaTooLarge,
  kGrid,
};

struct AnalyzeResult {
  std::optional<AcceptedFrame> frame;
  RejectReason reason = RejectReason::kNoChessboard;
  std::vector<cv::Point2f> corners;
  double blur = 0.0;
  double mean = 0.0;
  double area = 0.0;
  int grid = 0;
  std::uint64_t sequence = 0;
};

struct AnalysisStats {
  std::uint64_t analyzed = 0;
  std::uint64_t accepted = 0;
  std::array<std::uint64_t, 7> rejected{};
};

struct CoverageState {
  int spatial_cells = 0;
  bool front = false;
  bool tilt_left = false;
  bool tilt_right = false;
  bool tilt_up = false;
  bool tilt_down = false;
  bool near = false;
  bool mid = false;
  bool far = false;

  bool pose_complete() const {
    return front && tilt_left && tilt_right && tilt_up && tilt_down;
  }
  bool distance_complete() const {
    return near && mid && far;
  }
  bool complete() const {
    return spatial_cells >= 5 && pose_complete() && distance_complete();
  }
};

struct CaptureEvaluation {
  CoverageState coverage;
  std::string guidance;
  bool complete = false;
  double provisional_rms = 0.0;
};

void Usage();
ParseResult ParseOptions(int argc, char** argv, Options* options);
cv::Mat ToBgr(const cockpit::camera::CameraFrame& frame);
AnalyzeResult Analyze(const cv::Mat& image, std::uint64_t sequence, const Options& options);
std::vector<std::filesystem::path> ImageFiles(const std::filesystem::path& directory);
CoverageState BuildCoverage(const Options& options, const std::vector<AcceptedFrame>& frames);
CaptureEvaluation EvaluateCapture(const Options& options,
                                  const std::vector<AcceptedFrame>& candidates);
std::string GuidanceNext(const Options& options, std::vector<AcceptedFrame> candidates);
bool CaptureComplete(const Options& options, const std::vector<AcceptedFrame>& candidates);
bool FinalValidator(const Options& options, const CoverageState& coverage);
bool safeToRemove(const Options& options, const std::vector<AcceptedFrame>& frames);
bool Calibrate(const Options& options, std::vector<AcceptedFrame> accepted);

}  // namespace cockpit::camera_calibrator
