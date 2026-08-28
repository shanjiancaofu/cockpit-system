#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cockpit/modules/camera/capture/gstreamer_preview_pipeline.h"

namespace {

struct Options {
  std::string device = "nvargus://0";
  std::filesystem::path input_dir;
  std::filesystem::path output_dir = "_output/runtime/camera-calibration";
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
};

struct AcceptedFrame {
  cv::Mat image;
  std::vector<cv::Point2f> corners;
  double blur = 0.0;
  double mean = 0.0;
  double area = 0.0;
  int grid = 0;
  std::uint64_t sequence = 0;
};

void Usage() {
  std::cout << R"(camera-calibrator [options]
  --device nvargus://0       Capture device (default: nvargus://0)
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

bool ParseOptions(int argc, char** argv, Options* options) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      Usage();
      return false;
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
      if (!require_value("--device")) return false;
      options->device = value;
    } else if (arg == "--input-dir") {
      if (!require_value("--input-dir")) return false;
      options->input_dir = value;
    } else if (arg == "--output-dir") {
      if (!require_value("--output-dir")) return false;
      options->output_dir = value;
    } else if (arg == "--width") {
      if (!require_value("--width") || !ParseInt(value, &options->width)) return false;
    } else if (arg == "--height") {
      if (!require_value("--height") || !ParseInt(value, &options->height)) return false;
    } else if (arg == "--fps") {
      if (!require_value("--fps") || !ParseInt(value, &options->fps)) return false;
    } else if (arg == "--frames") {
      if (!require_value("--frames") || !ParseInt(value, &options->frames)) return false;
    } else if (arg == "--timeout-seconds") {
      if (!require_value("--timeout-seconds") || !ParseInt(value, &options->timeout_seconds)) return false;
    } else if (arg == "--corners-x") {
      if (!require_value("--corners-x") || !ParseInt(value, &options->corners_x)) return false;
    } else if (arg == "--corners-y") {
      if (!require_value("--corners-y") || !ParseInt(value, &options->corners_y)) return false;
    } else if (arg == "--square-size") {
      if (!require_value("--square-size") || !ParseDouble(value, &options->square_size)) return false;
    } else if (arg == "--blur-min") {
      if (!require_value("--blur-min") || !ParseDouble(value, &options->blur_min)) return false;
    } else if (arg == "--mean-min") {
      if (!require_value("--mean-min") || !ParseDouble(value, &options->mean_min)) return false;
    } else if (arg == "--mean-max") {
      if (!require_value("--mean-max") || !ParseDouble(value, &options->mean_max)) return false;
    } else if (arg == "--area-min") {
      if (!require_value("--area-min") || !ParseDouble(value, &options->area_min)) return false;
    } else if (arg == "--area-max") {
      if (!require_value("--area-max") || !ParseDouble(value, &options->area_max)) return false;
    } else if (arg == "--grid-required") {
      if (!require_value("--grid-required") || !ParseInt(value, &options->grid_required)) return false;
    } else if (arg == "--duplicate-threshold") {
      if (!require_value("--duplicate-threshold") || !ParseDouble(value, &options->duplicate_threshold)) return false;
    } else {
      std::cerr << "unknown option: " << arg << "\n";
      Usage();
      return false;
    }
  }
  if (options->width <= 0 || options->height <= 0 || options->fps <= 0 || options->frames <= 0 ||
      options->timeout_seconds <= 0 || options->corners_x <= 1 || options->corners_y <= 1 ||
      options->square_size <= 0.0 || options->area_min < 0.0 || options->area_max > 1.0 ||
      options->area_min >= options->area_max || options->grid_required < 1 ||
      options->grid_required > 9 || options->duplicate_threshold < 0.0) {
    std::cerr << "invalid calibration options\n";
    return false;
  }
  return true;
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
  if (image.empty()) return std::nullopt;
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
  const double area = static_cast<double>(bounds.area()) / static_cast<double>(image.cols * image.rows);
  const int grid = GridCoverage(corners, image.size());
  if (blur < options.blur_min || mean < options.mean_min || mean > options.mean_max ||
      area < options.area_min || area > options.area_max || grid < options.grid_required ||
      SimilarToAccepted(image, accepted, options.duplicate_threshold)) {
    return std::nullopt;
  }
  return AcceptedFrame{image.clone(), std::move(corners), blur, mean, area, grid, sequence};
}

std::vector<std::filesystem::path> ImageFiles(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension().string();
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

void WriteJson(const std::filesystem::path& path, const Options& options,
               const std::vector<AcceptedFrame>& accepted, double rms, double mean_error,
               double max_error, bool pass) {
  std::ofstream output(path);
  output << std::fixed << std::setprecision(6);
  output << "{\n  \"pass\": " << (pass ? "true" : "false") << ",\n"
         << "  \"image_width\": " << options.width << ",\n"
         << "  \"image_height\": " << options.height << ",\n"
         << "  \"board_corners_x\": " << options.corners_x << ",\n"
         << "  \"board_corners_y\": " << options.corners_y << ",\n"
         << "  \"accepted_samples\": " << accepted.size() << ",\n"
         << "  \"rms\": " << rms << ",\n"
         << "  \"mean_reprojection_error_px\": " << mean_error << ",\n"
         << "  \"max_reprojection_error_px\": " << max_error << "\n}\n";
}

bool Calibrate(const Options& options, const std::vector<AcceptedFrame>& accepted) {
  if (accepted.size() < 10) {
    std::cerr << "FAIL: need at least 10 accepted samples, got " << accepted.size() << "\n";
    return false;
  }
  std::vector<std::vector<cv::Point3f>> object_points(accepted.size());
  std::vector<std::vector<cv::Point2f>> image_points;
  const cv::Size image_size = accepted.front().image.size();
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
  cv::Mat distortion = cv::Mat::zeros(5, 1, CV_64F);
  std::vector<cv::Mat> rotations, translations;
  const double rms = cv::calibrateCamera(object_points, image_points, image_size, camera_matrix,
                                          distortion, rotations, translations);
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
    error_sum += view_error;
    max_error = std::max(max_error, view_error);
  }
  const double mean_error = error_sum / static_cast<double>(per_view.size());
  const bool pass = accepted.size() >= 20 && mean_error < 1.0 && max_error < 2.0 &&
                    std::isfinite(camera_matrix.at<double>(0, 0));
  std::filesystem::create_directories(options.output_dir);
  const auto yaml_path = options.output_dir / "camera_calibration.yaml";
  const auto csv_path = options.output_dir / "per_view_errors.csv";
  const auto json_path = options.output_dir / "calibration_report.json";
  const auto preview_path = options.output_dir / "undistorted_preview.jpg";
  cv::FileStorage yaml(yaml_path.string(), cv::FileStorage::WRITE);
  yaml << "image_width" << image_size.width << "image_height" << image_size.height
       << "fx" << camera_matrix.at<double>(0, 0) << "fy" << camera_matrix.at<double>(1, 1)
       << "cx" << camera_matrix.at<double>(0, 2) << "cy" << camera_matrix.at<double>(1, 2)
       << "distortion_model" << "plumb_bob" << "k1" << distortion.at<double>(0)
       << "k2" << distortion.at<double>(1) << "p1" << distortion.at<double>(2)
       << "p2" << distortion.at<double>(3) << "k3" << distortion.at<double>(4)
       << "rms" << rms << "mean_reprojection_error_px" << mean_error
       << "max_reprojection_error_px" << max_error;
  yaml.release();
  std::ofstream csv(csv_path);
  csv << "view,sequence,blur,mean_gray,area,grid_coverage,reprojection_error_px\n";
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    csv << index << ',' << accepted[index].sequence << ',' << accepted[index].blur << ','
        << accepted[index].mean << ',' << accepted[index].area << ',' << accepted[index].grid << ','
        << per_view[index] << '\n';
  }
  cv::Mat undistorted;
  cv::undistort(accepted.front().image, undistorted, camera_matrix, distortion);
  cv::imwrite(preview_path.string(), undistorted);
  WriteJson(json_path, options, accepted, rms, mean_error, max_error, pass);
  std::cout << std::fixed << std::setprecision(4)
            << "accepted_samples=" << accepted.size() << "\n"
            << "fx=" << camera_matrix.at<double>(0, 0) << " fy=" << camera_matrix.at<double>(1, 1)
            << " cx=" << camera_matrix.at<double>(0, 2) << " cy=" << camera_matrix.at<double>(1, 2)
            << "\n"
            << "k1=" << distortion.at<double>(0) << " k2=" << distortion.at<double>(1)
            << " p1=" << distortion.at<double>(2) << " p2=" << distortion.at<double>(3)
            << " k3=" << distortion.at<double>(4) << "\n"
            << "rms=" << rms << " mean_reprojection_error_px=" << mean_error
            << " max_reprojection_error_px=" << max_error << "\n"
            << "status=" << (pass ? "PASS" : "FAIL") << "\n"
            << "yaml=" << yaml_path << "\n"
            << "json=" << json_path << "\n"
            << "csv=" << csv_path << "\n"
            << "undistorted_preview=" << preview_path << "\n";
  return pass;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) return argc == 1 ? 2 : 0;
  std::vector<AcceptedFrame> accepted;
  if (!options.input_dir.empty()) {
    if (!std::filesystem::is_directory(options.input_dir)) {
      std::cerr << "input directory does not exist: " << options.input_dir << '\n';
      return 2;
    }
    for (const auto& file : ImageFiles(options.input_dir)) {
      cv::Mat image = cv::imread(file.string(), cv::IMREAD_COLOR);
      auto analyzed = Analyze(image, accepted.size() + 1, options, accepted);
      if (analyzed.has_value()) accepted.push_back(std::move(*analyzed));
      if (static_cast<int>(accepted.size()) >= options.frames) break;
    }
  } else {
    cockpit::camera::GstreamerPreviewPipeline pipeline;
    std::mutex mutex;
    std::condition_variable condition;
    std::string error;
    const bool started = pipeline.Start(
        cockpit::camera::CameraPreviewConfig{options.device, static_cast<std::uint32_t>(options.width),
                                             static_cast<std::uint32_t>(options.height),
                                             static_cast<std::uint32_t>(options.fps),
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.timeout_seconds);
    {
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait_until(lock, deadline, [&] {
        return static_cast<int>(accepted.size()) >= options.frames;
      });
    }
    pipeline.Stop();
    if (static_cast<int>(accepted.size()) < options.frames) {
      std::cerr << "capture timed out: accepted " << accepted.size() << '/' << options.frames << "\n";
      return 1;
    }
  }
  return Calibrate(options, accepted) ? 0 : 1;
}
