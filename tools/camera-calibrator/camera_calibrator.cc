#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cockpit/modules/camera/capture/argus_isp_preview_source.h"

namespace {

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

enum class ParseResult {
  kOk,
  kHelp,
  kError,
};

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

void Usage() {
  std::cout << R"(camera-calibrator [options]
  --device nvargus://0       Capture device (default: nvargus://0)
  --board-profile NAME        Board profile: q12-70-5 (12x9 squares, 11x8 corners, 5 mm)
  --input-dir DIR             Calibrate existing image files instead of capturing
  --output-dir DIR            Output directory
  --width N --height N        Capture dimensions (default: 1920x1080)
  --fps N                     Capture frame rate (default: 30)
  --frames N                  Accepted samples to collect (default: 30)
  --timeout-seconds N         Capture timeout (default: 120)
  --corners-x N --corners-y N Chessboard inner corners (default: 9x6)
  --square-size M             Square size in metres (default: 0.025)
  --blur-min N                Laplacian variance threshold
  --mean-min N --mean-max N   Mean grayscale range
  --area-min N --area-max N   Chessboard image-area range
  --grid-required N           Minimum occupied 3x3 cells
  --duplicate-threshold N     Mean absolute image difference threshold
  --help
)";
}

bool TakeValue(int& index, int argc, char** argv, std::string* value) {
  if (index + 1 >= argc) {
    return false;
  }
  *value = argv[++index];
  return true;
}

bool ParseInt(const std::string& text, int* value) {
  try {
    std::size_t consumed = 0;
    const int parsed = std::stoi(text, &consumed);
    if (consumed != text.size()) return false;
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseDouble(const std::string& text, double* value) {
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

ParseResult ParseOptions(int argc, char** argv, Options* options) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      Usage();
      return ParseResult::kHelp;
    }
    std::string value;
    auto require_value = [&](const char* name) {
      if (!TakeValue(index, argc, argv, &value)) {
        std::cerr << name << " requires a value\n";
        return false;
      }
      return true;
    };
    if (arg == "--device") {
      if (!require_value("--device")) return ParseResult::kError;
      options->device = value;
    } else if (arg == "--board-profile") {
      if (!require_value("--board-profile")) return ParseResult::kError;
      options->board_profile = value;
    } else if (arg == "--input-dir") {
      if (!require_value("--input-dir")) return ParseResult::kError;
      options->input_dir = value;
    } else if (arg == "--output-dir") {
      if (!require_value("--output-dir")) return ParseResult::kError;
      options->output_dir = value;
    } else if (arg == "--width") {
      if (!require_value("--width") || !ParseInt(value, &options->width))
        return ParseResult::kError;
    } else if (arg == "--height") {
      if (!require_value("--height") || !ParseInt(value, &options->height))
        return ParseResult::kError;
    } else if (arg == "--fps") {
      if (!require_value("--fps") || !ParseInt(value, &options->fps)) return ParseResult::kError;
    } else if (arg == "--frames") {
      if (!require_value("--frames") || !ParseInt(value, &options->frames))
        return ParseResult::kError;
    } else if (arg == "--timeout-seconds") {
      if (!require_value("--timeout-seconds") || !ParseInt(value, &options->timeout_seconds))
        return ParseResult::kError;
    } else if (arg == "--corners-x") {
      if (!require_value("--corners-x") || !ParseInt(value, &options->corners_x))
        return ParseResult::kError;
      options->corners_x_explicit = true;
    } else if (arg == "--corners-y") {
      if (!require_value("--corners-y") || !ParseInt(value, &options->corners_y))
        return ParseResult::kError;
      options->corners_y_explicit = true;
    } else if (arg == "--square-size") {
      if (!require_value("--square-size") || !ParseDouble(value, &options->square_size))
        return ParseResult::kError;
      options->square_size_explicit = true;
    } else if (arg == "--blur-min") {
      if (!require_value("--blur-min") || !ParseDouble(value, &options->blur_min))
        return ParseResult::kError;
    } else if (arg == "--mean-min") {
      if (!require_value("--mean-min") || !ParseDouble(value, &options->mean_min))
        return ParseResult::kError;
    } else if (arg == "--mean-max") {
      if (!require_value("--mean-max") || !ParseDouble(value, &options->mean_max))
        return ParseResult::kError;
    } else if (arg == "--area-min") {
      if (!require_value("--area-min") || !ParseDouble(value, &options->area_min))
        return ParseResult::kError;
    } else if (arg == "--area-max") {
      if (!require_value("--area-max") || !ParseDouble(value, &options->area_max))
        return ParseResult::kError;
    } else if (arg == "--grid-required") {
      if (!require_value("--grid-required") || !ParseInt(value, &options->grid_required))
        return ParseResult::kError;
    } else if (arg == "--duplicate-threshold") {
      if (!require_value("--duplicate-threshold") ||
          !ParseDouble(value, &options->duplicate_threshold))
        return ParseResult::kError;
    } else {
      std::cerr << "unknown option: " << arg << "\n";
      Usage();
      return ParseResult::kError;
    }
  }
  if (!options->board_profile.empty()) {
    if (options->board_profile != "q12-70-5") {
      std::cerr << "unknown board profile: " << options->board_profile << "\n";
      return ParseResult::kError;
    }
    if ((options->corners_x_explicit && options->corners_x != 11) ||
        (options->corners_y_explicit && options->corners_y != 8) ||
        (options->square_size_explicit && options->square_size != 0.005)) {
      std::cerr << "board profile q12-70-5 conflicts with explicit board geometry\n";
      return ParseResult::kError;
    }
    options->corners_x = 11;
    options->corners_y = 8;
    options->square_size = 0.005;
  }
  if (options->width <= 0 || options->height <= 0 || options->fps <= 0 || options->frames <= 0 ||
      options->timeout_seconds <= 0 || options->corners_x <= 1 || options->corners_y <= 1 ||
      options->square_size <= 0.0 || options->area_min < 0.0 || options->area_max > 1.0 ||
      options->area_min >= options->area_max || options->grid_required < 1 ||
      options->grid_required > 9 || options->duplicate_threshold < 0.0) {
    std::cerr << "invalid calibration options\n";
    return ParseResult::kError;
  }
  return ParseResult::kOk;
}

cv::Mat ToBgr(const cockpit::camera::CameraFrame& frame) {
  if (frame.format != cockpit::camera::CameraPixelFormat::kBgrx || frame.width == 0 ||
      frame.height == 0) {
    return {};
  }
  cv::Mat bgrx(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC4,
               const_cast<std::uint8_t*>(frame.data.data()), frame.stride_bytes);
  cv::Mat bgr;
  cv::cvtColor(bgrx, bgr, cv::COLOR_BGRA2BGR);
  return bgr.clone();
}

double BlurScore(const cv::Mat& gray) {
  cv::Mat laplacian;
  cv::Laplacian(gray, laplacian, CV_64F);
  cv::Scalar mean;
  cv::Scalar stddev;
  cv::meanStdDev(laplacian, mean, stddev);
  return stddev[0] * stddev[0];
}

double Median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2U;
  return values.size() % 2U == 0U ? (values[middle - 1U] + values[middle]) / 2.0 : values[middle];
}

void EstimatePose(AcceptedFrame* frame, const std::vector<cv::Point3f>& object_points,
                  const cv::Mat& camera_matrix, const cv::Mat& distortion) {
  cv::Mat rvec;
  cv::Mat tvec;
  if (!cv::solvePnP(object_points, frame->corners, camera_matrix, distortion, rvec, tvec)) {
    return;
  }
  cv::Mat rotation;
  cv::Rodrigues(rvec, rotation);
  frame->yaw_deg = std::atan2(rotation.at<double>(1, 0), rotation.at<double>(0, 0)) * 180.0 / CV_PI;
  frame->pitch_deg = std::atan2(-rotation.at<double>(2, 0),
                                std::hypot(rotation.at<double>(2, 1), rotation.at<double>(2, 2))) *
                     180.0 / CV_PI;
  frame->roll_deg =
      std::atan2(rotation.at<double>(2, 1), rotation.at<double>(2, 2)) * 180.0 / CV_PI;
  frame->distance_m = cv::norm(tvec);
  frame->pose_valid = std::isfinite(frame->yaw_deg) && std::isfinite(frame->pitch_deg) &&
                      std::isfinite(frame->roll_deg) && std::isfinite(frame->distance_m);
}

std::string PoseBucket(const AcceptedFrame& frame) {
  if (!frame.pose_valid) return "UNKNOWN";
  if (std::abs(frame.yaw_deg) < 12.0 && std::abs(frame.pitch_deg) < 12.0) return "FRONT";
  if (frame.yaw_deg <= -12.0) return "YAW_LEFT";
  if (frame.yaw_deg >= 12.0) return "YAW_RIGHT";
  if (frame.pitch_deg <= -12.0) return "PITCH_UP";
  return "PITCH_DOWN";
}

std::string DistanceBucket(const AcceptedFrame& frame) {
  if (!frame.pose_valid) return "UNKNOWN";
  if (frame.distance_m < 0.25) return "NEAR";
  if (frame.distance_m > 0.55) return "FAR";
  return "MID";
}

int GridCoverage(const std::vector<cv::Point2f>& corners, const cv::Size& image_size) {
  bool occupied[3][3] = {};
  for (const auto& point : corners) {
    const int x = std::clamp(static_cast<int>(point.x / image_size.width * 3.0), 0, 2);
    const int y = std::clamp(static_cast<int>(point.y / image_size.height * 3.0), 0, 2);
    occupied[y][x] = true;
  }
  int count = 0;
  for (const auto& row : occupied) {
    for (bool cell : row) count += cell ? 1 : 0;
  }
  return count;
}

bool SimilarToAccepted(const cv::Mat& image, const std::vector<AcceptedFrame>& accepted,
                       double threshold) {
  if (accepted.empty()) return false;
  cv::Mat small;
  cv::resize(image, small, cv::Size(32, 18), 0.0, 0.0, cv::INTER_AREA);
  small.convertTo(small, CV_32F);
  for (const auto& previous : accepted) {
    cv::Mat previous_small;
    cv::resize(previous.image, previous_small, cv::Size(32, 18), 0.0, 0.0, cv::INTER_AREA);
    previous_small.convertTo(previous_small, CV_32F);
    cv::Scalar difference = cv::mean(cv::abs(small - previous_small));
    if (difference[0] < threshold) return true;
  }
  return false;
}

std::optional<AcceptedFrame> Analyze(const cv::Mat& image, std::uint64_t sequence,
                                     const Options& options,
                                     const std::vector<AcceptedFrame>& accepted) {
  if (image.empty() || image.cols <= options.corners_x || image.rows <= options.corners_y) {
    return std::nullopt;
  }
  cv::Mat gray;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  std::vector<cv::Point2f> corners;
  const cv::Size pattern(options.corners_x, options.corners_y);
  const bool found = cv::findChessboardCorners(
      gray, pattern, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
  if (!found) return std::nullopt;
  cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                   cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 30, 0.01));
  const double blur = BlurScore(gray);
  const double mean = cv::mean(gray)[0];
  const cv::Rect bounds = cv::boundingRect(corners);
  const double area =
      static_cast<double>(bounds.area()) / static_cast<double>(image.cols * image.rows);
  const int grid = GridCoverage(corners, image.size());
  if (blur < options.blur_min || mean < options.mean_min || mean > options.mean_max ||
      area < options.area_min || area > options.area_max || grid < options.grid_required ||
      SimilarToAccepted(image, accepted, options.duplicate_threshold)) {
    return std::nullopt;
  }
  const double center_x = std::accumulate(corners.begin(), corners.end(), 0.0,
                                          [](double value, const cv::Point2f& point) {
                                            return value + point.x;
                                          }) /
                          static_cast<double>(corners.size()) / static_cast<double>(image.cols);
  const double center_y = std::accumulate(corners.begin(), corners.end(), 0.0,
                                          [](double value, const cv::Point2f& point) {
                                            return value + point.y;
                                          }) /
                          static_cast<double>(corners.size()) / static_cast<double>(image.rows);
  AcceptedFrame result{image.clone(), std::move(corners), blur, mean, area, grid, sequence};
  result.center_x_normalized = center_x;
  result.center_y_normalized = center_y;
  return result;
}

std::vector<std::filesystem::path> ImageFiles(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension().string();
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" ||
        extension == ".ppm") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

void WriteJson(const std::filesystem::path& path, const Options& options,
               const cv::Size& image_size, const std::vector<AcceptedFrame>& accepted, double rms,
               double mean_error, double median_error, double mad_error, double p95_error,
               double max_error, int spatial_coverage, bool pose_coverage, bool distance_coverage,
               bool pass) {
  std::ofstream output(path);
  output << std::fixed << std::setprecision(6);
  output << "{\n  \"pass\": " << (pass ? "true" : "false") << ",\n"
         << "  \"image_width\": " << image_size.width << ",\n"
         << "  \"image_height\": " << image_size.height << ",\n"
         << "  \"board_corners_x\": " << options.corners_x << ",\n"
         << "  \"board_corners_y\": " << options.corners_y << ",\n"
         << "  \"accepted_samples\": " << accepted.size() << ",\n"
         << "  \"selected_samples\": " << accepted.size() << ",\n"
         << "  \"outlier_samples\": "
         << std::count_if(accepted.begin(), accepted.end(),
                          [](const AcceptedFrame& frame) {
                            return frame.outlier;
                          })
         << ",\n"
         << "  \"rms\": " << rms << ",\n"
         << "  \"mean_reprojection_error_px\": " << mean_error << ",\n"
         << "  \"median_reprojection_error_px\": " << median_error << ",\n"
         << "  \"mad_reprojection_error_px\": " << mad_error << ",\n"
         << "  \"p95_reprojection_error_px\": " << p95_error << ",\n"
         << "  \"max_reprojection_error_px\": " << max_error << ",\n"
         << "  \"spatial_coverage\": " << spatial_coverage << ",\n"
         << "  \"pose_coverage\": " << (pose_coverage ? "true" : "false") << ",\n"
         << "  \"distance_coverage\": " << (distance_coverage ? "true" : "false") << ",\n"
         << "  \"verification\": \"UNVERIFIED\",\n  \"observations\": [\n";
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    const auto& frame = accepted[index];
    output << "    {\"sequence\": " << frame.sequence
           << ", \"selected\": true, \"outlier\": " << (frame.outlier ? "true" : "false")
           << ", \"center_x\": " << frame.center_x_normalized
           << ", \"center_y\": " << frame.center_y_normalized << ", \"yaw_deg\": " << frame.yaw_deg
           << ", \"pitch_deg\": " << frame.pitch_deg << ", \"roll_deg\": " << frame.roll_deg
           << ", \"distance_m\": " << frame.distance_m
           << ", \"reprojection_error_px\": " << frame.reprojection_error_px << "}"
           << (index + 1U == accepted.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
}

bool Calibrate(const Options& options, std::vector<AcceptedFrame> accepted) {
  if (accepted.size() < 10) {
    std::cerr << "FAIL: need at least 10 accepted samples, got " << accepted.size() << "\n";
    return false;
  }
  std::vector<std::vector<cv::Point3f>> object_points(accepted.size());
  std::vector<std::vector<cv::Point2f>> image_points;
  const cv::Size image_size = accepted.front().image.size();
  if (std::any_of(accepted.begin(), accepted.end(), [&image_size](const AcceptedFrame& frame) {
        return frame.image.size() != image_size;
      })) {
    std::cerr << "FAIL: accepted calibration images do not have a uniform resolution\n";
    return false;
  }
  for (auto& frame : object_points) {
    for (int y = 0; y < options.corners_y; ++y) {
      for (int x = 0; x < options.corners_x; ++x) {
        frame.emplace_back(static_cast<float>(x * options.square_size),
                           static_cast<float>(y * options.square_size), 0.0F);
      }
    }
  }
  for (const auto& frame : accepted) image_points.push_back(frame.corners);
  cv::Mat camera_matrix = cv::Mat::eye(3, 3, CV_64F);
  camera_matrix.at<double>(0, 0) = static_cast<double>(image_size.width);
  camera_matrix.at<double>(1, 1) = static_cast<double>(image_size.width);
  camera_matrix.at<double>(0, 2) = static_cast<double>(image_size.width) / 2.0;
  camera_matrix.at<double>(1, 2) = static_cast<double>(image_size.height) / 2.0;
  cv::Mat distortion = cv::Mat::zeros(5, 1, CV_64F);
  std::vector<cv::Mat> rotations, translations;
  const double rms =
      cv::calibrateCamera(object_points, image_points, image_size, camera_matrix, distortion,
                          rotations, translations, cv::CALIB_USE_INTRINSIC_GUESS);
  double error_sum = 0.0;
  double max_error = 0.0;
  std::vector<double> per_view;
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    std::vector<cv::Point2f> projected;
    cv::projectPoints(object_points[index], rotations[index], translations[index], camera_matrix,
                      distortion, projected);
    double sum = 0.0;
    for (std::size_t point = 0; point < projected.size(); ++point) {
      sum += cv::norm(projected[point] - image_points[index][point]);
    }
    const double view_error = sum / static_cast<double>(projected.size());
    per_view.push_back(view_error);
    accepted[index].reprojection_error_px = view_error;
    EstimatePose(&accepted[index], object_points[index], camera_matrix, distortion);
    error_sum += view_error;
    max_error = std::max(max_error, view_error);
  }
  const double mean_error = error_sum / static_cast<double>(per_view.size());
  const double median_error = Median(per_view);
  std::vector<double> absolute_deviations;
  absolute_deviations.reserve(per_view.size());
  for (const double value : per_view) absolute_deviations.push_back(std::abs(value - median_error));
  const double mad_error = Median(absolute_deviations);
  const double p95_error = [&per_view] {
    if (per_view.empty()) return 0.0;
    std::vector<double> sorted = per_view;
    std::sort(sorted.begin(), sorted.end());
    const auto index = static_cast<std::size_t>(0.95 * static_cast<double>(sorted.size() - 1U));
    return sorted[index];
  }();
  const int spatial_coverage = [&accepted] {
    bool cells[3][3] = {};
    for (const auto& frame : accepted) {
      const int x = std::clamp(static_cast<int>(frame.center_x_normalized * 3.0), 0, 2);
      const int y = std::clamp(static_cast<int>(frame.center_y_normalized * 3.0), 0, 2);
      cells[y][x] = true;
    }
    int count = 0;
    for (const auto& row : cells) {
      for (const bool cell : row) count += cell ? 1 : 0;
    }
    return count;
  }();
  std::vector<std::string> pose_buckets;
  std::vector<std::string> distance_buckets;
  for (const auto& frame : accepted) {
    pose_buckets.push_back(PoseBucket(frame));
    distance_buckets.push_back(DistanceBucket(frame));
  }
  const auto has_bucket = [](const std::vector<std::string>& buckets, const char* wanted) {
    return std::find(buckets.begin(), buckets.end(), wanted) != buckets.end();
  };
  const bool pose_coverage =
      has_bucket(pose_buckets, "FRONT") &&
      (has_bucket(pose_buckets, "YAW_LEFT") || has_bucket(pose_buckets, "YAW_RIGHT"));
  const bool distance_coverage = has_bucket(distance_buckets, "NEAR") &&
                                 has_bucket(distance_buckets, "MID") &&
                                 has_bucket(distance_buckets, "FAR");
  const double outlier_limit = std::max(2.0, median_error + 3.0 * 1.4826 * mad_error);
  for (auto& frame : accepted) frame.outlier = frame.reprojection_error_px > outlier_limit;
  const bool pass = accepted.size() >= 20 && mean_error < 1.0 && max_error < 2.0 &&
                    std::isfinite(camera_matrix.at<double>(0, 0));
  std::filesystem::create_directories(options.output_dir);
  const auto yaml_path = options.output_dir / "camera_calibration.yaml";
  const auto csv_path = options.output_dir / "per_view_errors.csv";
  const auto json_path = options.output_dir / "calibration_report.json";
  const auto preview_path = options.output_dir / "undistorted_preview.jpg";
  const auto original_preview_path = options.output_dir / "original_preview.jpg";
  cv::FileStorage yaml(yaml_path.string(), cv::FileStorage::WRITE);
  yaml << "image_width" << image_size.width << "image_height" << image_size.height << "fx"
       << camera_matrix.at<double>(0, 0) << "fy" << camera_matrix.at<double>(1, 1) << "cx"
       << camera_matrix.at<double>(0, 2) << "cy" << camera_matrix.at<double>(1, 2)
       << "distortion_model"
       << "plumb_bob"
       << "k1" << distortion.at<double>(0) << "k2" << distortion.at<double>(1) << "p1"
       << distortion.at<double>(2) << "p2" << distortion.at<double>(3) << "k3"
       << distortion.at<double>(4) << "rms" << rms << "mean_reprojection_error_px" << mean_error
       << "max_reprojection_error_px" << max_error;
  yaml.release();
  std::ofstream csv(csv_path);
  csv << "view,sequence,selected,outlier,blur,mean_gray,area,grid_coverage,center_x,center_y,yaw_"
         "deg,pitch_deg,roll_deg,distance_m,pose_bucket,distance_bucket,reprojection_error_px\n";
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    csv << index << ',' << accepted[index].sequence << ',' << accepted[index].selected << ','
        << accepted[index].outlier << ',' << accepted[index].blur << ',' << accepted[index].mean
        << ',' << accepted[index].area << ',' << accepted[index].grid << ','
        << accepted[index].center_x_normalized << ',' << accepted[index].center_y_normalized << ','
        << accepted[index].yaw_deg << ',' << accepted[index].pitch_deg << ','
        << accepted[index].roll_deg << ',' << accepted[index].distance_m << ','
        << pose_buckets[index] << ',' << distance_buckets[index] << ',' << per_view[index] << '\n';
  }
  cv::Mat undistorted;
  cv::undistort(accepted.front().image, undistorted, camera_matrix, distortion);
  cv::imwrite(original_preview_path.string(), accepted.front().image);
  cv::imwrite(preview_path.string(), undistorted);
  WriteJson(json_path, options, image_size, accepted, rms, mean_error, median_error, mad_error,
            p95_error, max_error, spatial_coverage, pose_coverage, distance_coverage, pass);
  std::cout << std::fixed << std::setprecision(4) << "accepted_samples=" << accepted.size() << "\n"
            << "fx=" << camera_matrix.at<double>(0, 0) << " fy=" << camera_matrix.at<double>(1, 1)
            << " cx=" << camera_matrix.at<double>(0, 2) << " cy=" << camera_matrix.at<double>(1, 2)
            << "\n"
            << "k1=" << distortion.at<double>(0) << " k2=" << distortion.at<double>(1)
            << " p1=" << distortion.at<double>(2) << " p2=" << distortion.at<double>(3)
            << " k3=" << distortion.at<double>(4) << "\n"
            << "rms=" << rms << " mean_reprojection_error_px=" << mean_error
            << " median_reprojection_error_px=" << median_error
            << " mad_reprojection_error_px=" << mad_error
            << " p95_reprojection_error_px=" << p95_error
            << " max_reprojection_error_px=" << max_error
            << " spatial_coverage=" << spatial_coverage << " pose_coverage=" << pose_coverage
            << " distance_coverage=" << distance_coverage << "\n"
            << "status=" << (pass ? "PASS" : "FAIL") << "\n"
            << "yaml=" << yaml_path << "\n"
            << "json=" << json_path << "\n"
            << "csv=" << csv_path << "\n"
            << "original_preview=" << original_preview_path << "\n"
            << "undistorted_preview=" << preview_path << "\n";
  return pass;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  const ParseResult parse_result = ParseOptions(argc, argv, &options);
  if (parse_result == ParseResult::kHelp) return 0;
  if (parse_result == ParseResult::kError) return 2;
  std::vector<AcceptedFrame> accepted;
  if (!options.input_dir.empty()) {
    if (!std::filesystem::is_directory(options.input_dir)) {
      std::cerr << "input directory does not exist: " << options.input_dir << '\n';
      return 2;
    }
    const auto image_files = ImageFiles(options.input_dir);
    std::optional<cv::Size> expected_size;
    for (const auto& file : image_files) {
      cv::Mat image = cv::imread(file.string(), cv::IMREAD_COLOR);
      if (image.empty()) continue;
      if (!expected_size.has_value()) {
        expected_size = image.size();
      } else if (image.size() != *expected_size) {
        std::cerr << "input image resolution mismatch: " << file << " is " << image.cols << 'x'
                  << image.rows << ", expected " << expected_size->width << 'x'
                  << expected_size->height << '\n';
        return 2;
      }
    }
    for (const auto& file : image_files) {
      cv::Mat image = cv::imread(file.string(), cv::IMREAD_COLOR);
      if (image.empty()) continue;
      auto analyzed = Analyze(image, accepted.size() + 1, options, accepted);
      if (analyzed.has_value()) accepted.push_back(std::move(*analyzed));
      if (static_cast<int>(accepted.size()) >= options.frames) break;
    }
  } else {
    cockpit::camera::ArgusIspPreviewSource pipeline;
    std::mutex mutex;
    std::condition_variable condition;
    std::string error;
    const bool started = pipeline.Start(
        cockpit::camera::CameraPreviewConfig{
            options.device, static_cast<std::uint32_t>(options.width),
            static_cast<std::uint32_t>(options.height), static_cast<std::uint32_t>(options.fps),
            cockpit::camera::CameraPixelFormat::kBgrx},
        [&](cockpit::camera::CameraFrame frame) {
          cv::Mat image = ToBgr(frame);
          std::lock_guard<std::mutex> lock(mutex);
          auto analyzed = Analyze(image, frame.sequence, options, accepted);
          if (analyzed.has_value()) {
            accepted.push_back(std::move(*analyzed));
            condition.notify_one();
          }
        },
        &error);
    if (!started) {
      std::cerr << "capture failed: " << (error.empty() ? "unknown error" : error) << '\n';
      return 1;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(options.timeout_seconds);
    {
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait_until(lock, deadline, [&] {
        return static_cast<int>(accepted.size()) >= options.frames;
      });
    }
    pipeline.Stop();
    if (static_cast<int>(accepted.size()) < options.frames) {
      std::cerr << "capture timed out: accepted " << accepted.size() << '/' << options.frames
                << "\n";
      return 1;
    }
  }
  return Calibrate(options, accepted) ? 0 : 1;
}
