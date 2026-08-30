#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "camera_calibrator_internal.h"

#include "cockpit/modules/camera/capture/argus_isp_preview_source.h"

namespace cockpit::camera_calibrator {

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

const char* RejectReasonName(RejectReason reason) {
  switch (reason) {
    case RejectReason::kAccepted:
      return "ACCEPTED";
    case RejectReason::kNoChessboard:
      return "NO_BOARD";
    case RejectReason::kBlur:
      return "BLUR";
    case RejectReason::kExposure:
      return "EXPOSURE";
    case RejectReason::kAreaTooSmall:
      return "AREA_SMALL";
    case RejectReason::kAreaTooLarge:
      return "AREA_LARGE";
    case RejectReason::kGrid:
      return "GRID";
  }
  return "UNKNOWN";
}

std::size_t RejectReasonIndex(RejectReason reason) {
  return static_cast<std::size_t>(reason);
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
  const cv::Vec3d normal(rotation.at<double>(0, 2), rotation.at<double>(1, 2),
                         rotation.at<double>(2, 2));
  frame->horizontal_tilt_deg = std::atan2(normal[0], normal[2]) * 180.0 / CV_PI;
  frame->vertical_tilt_deg = std::atan2(normal[1], normal[2]) * 180.0 / CV_PI;
  frame->in_plane_rotation_deg =
      std::atan2(rotation.at<double>(1, 0), rotation.at<double>(0, 0)) * 180.0 / CV_PI;
  frame->distance_m = cv::norm(tvec);
  frame->pose_valid =
      std::isfinite(frame->horizontal_tilt_deg) && std::isfinite(frame->vertical_tilt_deg) &&
      std::isfinite(frame->in_plane_rotation_deg) && std::isfinite(frame->distance_m);
}

std::string PoseBucket(const AcceptedFrame& frame, const Options& options) {
  if (!frame.pose_valid) return "UNKNOWN";
  if (std::abs(frame.horizontal_tilt_deg) < options.tilt_threshold_deg &&
      std::abs(frame.vertical_tilt_deg) < options.tilt_threshold_deg)
    return "FRONT";
  if (frame.horizontal_tilt_deg <= -options.tilt_threshold_deg) return "TILT_LEFT";
  if (frame.horizontal_tilt_deg >= options.tilt_threshold_deg) return "TILT_RIGHT";
  if (frame.vertical_tilt_deg <= -options.tilt_threshold_deg) return "TILT_UP";
  return "TILT_DOWN";
}

bool IsFront(const AcceptedFrame& frame, const Options& options) {
  return frame.pose_valid && std::abs(frame.horizontal_tilt_deg) < options.tilt_threshold_deg &&
         std::abs(frame.vertical_tilt_deg) < options.tilt_threshold_deg;
}

bool IsTiltLeft(const AcceptedFrame& frame, const Options& options) {
  return frame.pose_valid && frame.horizontal_tilt_deg <= -options.tilt_threshold_deg;
}

bool IsTiltRight(const AcceptedFrame& frame, const Options& options) {
  return frame.pose_valid && frame.horizontal_tilt_deg >= options.tilt_threshold_deg;
}

bool IsTiltUp(const AcceptedFrame& frame, const Options& options) {
  return frame.pose_valid && frame.vertical_tilt_deg <= -options.tilt_threshold_deg;
}

bool IsTiltDown(const AcceptedFrame& frame, const Options& options) {
  return frame.pose_valid && frame.vertical_tilt_deg >= options.tilt_threshold_deg;
}

std::string DistanceBucket(const AcceptedFrame& frame, const Options& options) {
  if (!frame.pose_valid) return "UNKNOWN";
  if (frame.distance_m < options.near_distance_m) return "NEAR";
  if (frame.distance_m > options.far_distance_m) return "FAR";
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

AnalyzeResult Analyze(const cv::Mat& image, std::uint64_t sequence, const Options& options) {
  AnalyzeResult result;
  result.sequence = sequence;
  if (image.empty() || image.cols <= options.corners_x || image.rows <= options.corners_y) {
    return result;
  }
  cv::Mat gray;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  std::vector<cv::Point2f> corners;
  const cv::Size pattern(options.corners_x, options.corners_y);
  const bool found = cv::findChessboardCorners(
      gray, pattern, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
  if (!found) return result;
  result.corners = corners;
  cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                   cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 30, 0.01));
  result.corners = corners;
  const cv::Rect bounds = cv::boundingRect(corners);
  result.area = static_cast<double>(bounds.area()) / static_cast<double>(image.cols * image.rows);
  const int margin_x = std::max(1, bounds.width / 5);
  const int margin_y = std::max(1, bounds.height / 5);
  const cv::Rect roi =
      bounds & cv::Rect(std::max(0, bounds.x - margin_x), std::max(0, bounds.y - margin_y),
                        bounds.width + margin_x * 2, bounds.height + margin_y * 2);
  result.blur = BlurScore(gray(roi));
  result.mean = cv::mean(gray(roi))[0];
  result.grid = GridCoverage(corners, image.size());
  if (result.blur < options.blur_min) {
    result.reason = RejectReason::kBlur;
    return result;
  }
  if (result.mean < options.mean_min || result.mean > options.mean_max) {
    result.reason = RejectReason::kExposure;
    return result;
  }
  if (result.area < options.area_min) {
    result.reason = RejectReason::kAreaTooSmall;
    return result;
  }
  if (result.area > options.area_max) {
    result.reason = RejectReason::kAreaTooLarge;
    return result;
  }
  if (result.grid < options.grid_required) {
    result.reason = RejectReason::kGrid;
    return result;
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
  AcceptedFrame accepted_frame{image.clone(), std::move(corners), result.blur, result.mean,
                               result.area,   result.grid,        sequence};
  accepted_frame.center_x_normalized = center_x;
  accepted_frame.center_y_normalized = center_y;
  result.frame = std::move(accepted_frame);
  result.reason = RejectReason::kAccepted;
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
               std::size_t candidate_count, std::size_t removed_count,
               const std::string& failure_reason, bool pass) {
  std::ofstream output(path);
  output << std::fixed << std::setprecision(6);
  output << "{\n  \"pass\": " << (pass ? "true" : "false") << ",\n"
         << "  \"image_width\": " << image_size.width << ",\n"
         << "  \"image_height\": " << image_size.height << ",\n"
         << "  \"board_corners_x\": " << options.corners_x << ",\n"
         << "  \"board_corners_y\": " << options.corners_y << ",\n"
         << "  \"candidate_samples\": " << candidate_count << ",\n"
         << "  \"accepted_samples\": " << accepted.size() << ",\n"
         << "  \"selected_samples\": " << accepted.size() << ",\n"
         << "  \"outlier_samples\": " << removed_count << ",\n"
         << "  \"rms\": " << rms << ",\n"
         << "  \"mean_reprojection_error_px\": " << mean_error << ",\n"
         << "  \"median_reprojection_error_px\": " << median_error << ",\n"
         << "  \"mad_reprojection_error_px\": " << mad_error << ",\n"
         << "  \"p95_reprojection_error_px\": " << p95_error << ",\n"
         << "  \"max_reprojection_error_px\": " << max_error << ",\n"
         << "  \"spatial_coverage\": " << spatial_coverage << ",\n"
         << "  \"pose_coverage\": " << (pose_coverage ? "true" : "false") << ",\n"
         << "  \"distance_coverage\": " << (distance_coverage ? "true" : "false") << ",\n"
         << "  \"failure_reason\": \"" << failure_reason << "\",\n"
         << "  \"verification\": \"UNVERIFIED\",\n  \"observations\": [\n";
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    const auto& frame = accepted[index];
    output << "    {\"sequence\": " << frame.sequence
           << ", \"selected\": true, \"outlier\": " << (frame.outlier ? "true" : "false")
           << ", \"center_x\": " << frame.center_x_normalized
           << ", \"center_y\": " << frame.center_y_normalized
           << ", \"horizontal_tilt_deg\": " << frame.horizontal_tilt_deg
           << ", \"vertical_tilt_deg\": " << frame.vertical_tilt_deg
           << ", \"in_plane_rotation_deg\": " << frame.in_plane_rotation_deg
           << ", \"distance_m\": " << frame.distance_m
           << ", \"reprojection_error_px\": " << frame.reprojection_error_px << "}"
           << (index + 1U == accepted.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
}

struct CalibrationSolution {
  cv::Mat camera_matrix;
  cv::Mat distortion;
  double rms = std::numeric_limits<double>::infinity();
  std::vector<double> per_view;
};

std::vector<cv::Point3f> BoardObjectPoints(const Options& options) {
  std::vector<cv::Point3f> points;
  for (int y = 0; y < options.corners_y; ++y) {
    for (int x = 0; x < options.corners_x; ++x) {
      points.emplace_back(static_cast<float>(x * options.square_size),
                          static_cast<float>(y * options.square_size), 0.0F);
    }
  }
  return points;
}

CalibrationSolution SolveCalibration(const Options& options, std::vector<AcceptedFrame>* frames) {
  CalibrationSolution result;
  if (frames == nullptr || frames->size() < 10U) return result;
  const cv::Size image_size = frames->front().image.size();
  const auto board_points = BoardObjectPoints(options);
  std::vector<std::vector<cv::Point3f>> object_points(frames->size(), board_points);
  std::vector<std::vector<cv::Point2f>> image_points;
  for (const auto& frame : *frames) image_points.push_back(frame.corners);
  result.camera_matrix = cv::Mat::eye(3, 3, CV_64F);
  result.camera_matrix.at<double>(0, 0) = static_cast<double>(image_size.width);
  result.camera_matrix.at<double>(1, 1) = static_cast<double>(image_size.width);
  result.camera_matrix.at<double>(0, 2) = static_cast<double>(image_size.width) / 2.0;
  result.camera_matrix.at<double>(1, 2) = static_cast<double>(image_size.height) / 2.0;
  result.distortion = cv::Mat::zeros(5, 1, CV_64F);
  std::vector<cv::Mat> rotations;
  std::vector<cv::Mat> translations;
  result.rms = cv::calibrateCamera(object_points, image_points, image_size, result.camera_matrix,
                                   result.distortion, rotations, translations,
                                   cv::CALIB_USE_INTRINSIC_GUESS);
  for (std::size_t index = 0; index < frames->size(); ++index) {
    std::vector<cv::Point2f> projected;
    cv::projectPoints(board_points, rotations[index], translations[index], result.camera_matrix,
                      result.distortion, projected);
    double squared_error = 0.0;
    for (std::size_t point = 0; point < projected.size(); ++point) {
      const double error = cv::norm(projected[point] - image_points[index][point]);
      squared_error += error * error;
    }
    const double view_error = std::sqrt(squared_error / static_cast<double>(projected.size()));
    result.per_view.push_back(view_error);
    (*frames)[index].reprojection_error_px = view_error;
    EstimatePose(&(*frames)[index], board_points, result.camera_matrix, result.distortion);
  }
  return result;
}

double FeatureDistance(const AcceptedFrame& left, const AcceptedFrame& right) {
  const auto square = [](double value) {
    return value * value;
  };
  return std::sqrt(square((left.center_x_normalized - right.center_x_normalized) * 2.0) +
                   square((left.center_y_normalized - right.center_y_normalized) * 2.0) +
                   square(std::log(std::max(left.area, 1e-6) / std::max(right.area, 1e-6))) +
                   square((left.horizontal_tilt_deg - right.horizontal_tilt_deg) / 35.0) +
                   square((left.vertical_tilt_deg - right.vertical_tilt_deg) / 35.0) +
                   square((left.in_plane_rotation_deg - right.in_plane_rotation_deg) / 90.0) +
                   square((left.distance_m - right.distance_m) / 0.3));
}

std::string ScaleBucket(const AcceptedFrame& frame, double min_area, double max_area) {
  if (max_area <= min_area || min_area <= 0.0) return "UNKNOWN";
  const double span = std::log(max_area / min_area);
  if (span < std::log(1.5)) return "MID";
  const double position = std::log(std::max(frame.area, min_area) / min_area) / span;
  if (position < 1.0 / 3.0) return "FAR";
  if (position > 2.0 / 3.0) return "NEAR";
  return "MID";
}

std::vector<AcceptedFrame> SelectKeyframes(const Options& options,
                                           std::vector<AcceptedFrame> candidates,
                                           std::size_t target) {
  if (candidates.size() <= target) return candidates;
  std::vector<std::size_t> selected;
  const auto add_first_matching = [&selected, &candidates](const auto& predicate) {
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      if (std::find(selected.begin(), selected.end(), index) == selected.end() &&
          predicate(candidates[index])) {
        selected.push_back(index);
        return;
      }
    }
  };
  if (!options.board_profile.empty()) {
    add_first_matching([&options](const AcceptedFrame& frame) {
      return IsFront(frame, options);
    });
    add_first_matching([&options](const AcceptedFrame& frame) {
      return IsTiltLeft(frame, options);
    });
    add_first_matching([&options](const AcceptedFrame& frame) {
      return IsTiltRight(frame, options);
    });
    add_first_matching([&options](const AcceptedFrame& frame) {
      return IsTiltUp(frame, options);
    });
    add_first_matching([&options](const AcceptedFrame& frame) {
      return IsTiltDown(frame, options);
    });
    const auto area_range =
        std::minmax_element(candidates.begin(), candidates.end(),
                            [](const AcceptedFrame& left, const AcceptedFrame& right) {
                              return left.area < right.area;
                            });
    const double min_area = area_range.first->area;
    const double max_area = area_range.second->area;
    const char* const required_distances[] = {"NEAR", "MID", "FAR"};
    for (const char* required : required_distances) {
      add_first_matching([min_area, max_area, required](const AcceptedFrame& frame) {
        return ScaleBucket(frame, min_area, max_area) == required;
      });
    }
    for (int y = 0; y < 3; ++y) {
      for (int x = 0; x < 3; ++x) {
        add_first_matching([x, y](const AcceptedFrame& frame) {
          return std::clamp(static_cast<int>(frame.center_x_normalized * 3.0), 0, 2) == x &&
                 std::clamp(static_cast<int>(frame.center_y_normalized * 3.0), 0, 2) == y;
        });
      }
    }
  }
  if (selected.empty()) selected.push_back(0U);
  while (selected.size() < target) {
    double best_distance = -1.0;
    std::size_t best_index = 0U;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      if (std::find(selected.begin(), selected.end(), index) != selected.end()) continue;
      double nearest = std::numeric_limits<double>::infinity();
      for (const auto chosen : selected) {
        nearest = std::min(nearest, FeatureDistance(candidates[index], candidates[chosen]));
      }
      if (nearest > best_distance) {
        best_distance = nearest;
        best_index = index;
      }
    }
    selected.push_back(best_index);
  }
  std::vector<AcceptedFrame> result;
  for (const auto index : selected) result.push_back(std::move(candidates[index]));
  return result;
}

CoverageState BuildCoverage(const Options& options, const std::vector<AcceptedFrame>& frames) {
  CoverageState coverage;
  bool cells[3][3] = {};
  if (frames.empty()) return coverage;
  const auto area_range = std::minmax_element(
      frames.begin(), frames.end(), [](const AcceptedFrame& left, const AcceptedFrame& right) {
        return left.area < right.area;
      });
  const double min_area = area_range.first->area;
  const double max_area = area_range.second->area;
  for (const auto& frame : frames) {
    const int x = std::clamp(static_cast<int>(frame.center_x_normalized * 3.0), 0, 2);
    const int y = std::clamp(static_cast<int>(frame.center_y_normalized * 3.0), 0, 2);
    cells[y][x] = true;
    coverage.front = coverage.front || IsFront(frame, options);
    coverage.tilt_left = coverage.tilt_left || IsTiltLeft(frame, options);
    coverage.tilt_right = coverage.tilt_right || IsTiltRight(frame, options);
    coverage.tilt_up = coverage.tilt_up || IsTiltUp(frame, options);
    coverage.tilt_down = coverage.tilt_down || IsTiltDown(frame, options);
    const std::string scale = ScaleBucket(frame, min_area, max_area);
    coverage.near = coverage.near || scale == "NEAR";
    coverage.mid = coverage.mid || scale == "MID";
    coverage.far = coverage.far || scale == "FAR";
  }
  for (const auto& row : cells) {
    for (const bool cell : row) coverage.spatial_cells += cell ? 1 : 0;
  }
  return coverage;
}

bool FinalValidator(const Options& options, const CoverageState& coverage) {
  return options.board_profile.empty() || coverage.complete();
}

bool safeToRemove(const Options& options, const std::vector<AcceptedFrame>& frames) {
  return FinalValidator(options, BuildCoverage(options, frames));
}

std::string GuidanceNext(const Options& options, std::vector<AcceptedFrame> candidates) {
  return EvaluateCapture(options, candidates).guidance;
}

CaptureEvaluation EvaluateCapture(const Options& options,
                                  const std::vector<AcceptedFrame>& candidates) {
  CaptureEvaluation evaluation;
  if (candidates.empty()) {
    evaluation.guidance = "请将棋盘完整放入画面并保持清晰";
    return evaluation;
  }
  if (candidates.size() < 10U) {
    evaluation.guidance = "先采集基础样本";
    return evaluation;
  }
  auto solved_candidates = candidates;
  const auto solution = SolveCalibration(options, &solved_candidates);
  evaluation.provisional_rms = solution.rms;
  if (!std::isfinite(solution.rms)) {
    evaluation.guidance = "采集更多不同角度和位置";
    return evaluation;
  }
  const CoverageState coverage = BuildCoverage(options, solved_candidates);
  evaluation.coverage = coverage;
  if (coverage.spatial_cells < 5)
    evaluation.guidance = "移动棋盘，覆盖新的画面区域";
  else if (!coverage.front)
    evaluation.guidance = "正对棋盘，保持正面";
  else if (!coverage.tilt_left)
    evaluation.guidance = "将棋盘向左倾斜";
  else if (!coverage.tilt_right)
    evaluation.guidance = "将棋盘向右倾斜";
  else if (!coverage.tilt_up)
    evaluation.guidance = "将棋盘向上倾斜";
  else if (!coverage.tilt_down)
    evaluation.guidance = "将棋盘向下倾斜";
  else if (!coverage.near)
    evaluation.guidance = "将棋盘移近";
  else if (!coverage.mid)
    evaluation.guidance = "将棋盘移动到中距离";
  else if (!coverage.far)
    evaluation.guidance = "将棋盘移远（超过远距离阈值）";
  else if (candidates.size() < static_cast<std::size_t>(options.frames))
    evaluation.guidance = "覆盖完成，继续采集至目标样本数";
  else
    evaluation.guidance = "采集完成";
  evaluation.complete = candidates.size() >= static_cast<std::size_t>(options.frames) &&
                        FinalValidator(options, coverage);
  return evaluation;
}

bool ShowPreview(const Options& options, const cv::Mat& image,
                 const std::vector<AcceptedFrame>& candidates, std::uint64_t sequence,
                 const std::string& next, const AnalyzeResult& analysis) {
  static bool preview_disabled = false;
  static bool warning_printed = false;
  if (!options.preview || preview_disabled) return false;
  if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
    if (!warning_printed) {
      std::cerr
          << "preview unavailable: DISPLAY/WAYLAND_DISPLAY is not set; continuing in CLI mode\n";
      warning_printed = true;
    }
    preview_disabled = true;
    return false;
  }
  try {
    cv::Mat display =
        image.empty() ? cv::Mat(540, 960, CV_8UC3, cv::Scalar(24, 24, 24)) : image.clone();
    const CoverageState coverage = BuildCoverage(options, candidates);
    cv::rectangle(display, cv::Rect(0, 0, display.cols, 130), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(display,
                image.empty() ? "Waiting for camera frame..."
                              : "Frame: " + std::to_string(sequence) +
                                    "  Candidates: " + std::to_string(candidates.size()) +
                                    "  Spatial: " + std::to_string(coverage.spatial_cells) + "/5",
                cv::Point(20, 30), cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0, 255, 255), 2);
    const auto preview_instruction = [&next] {
      if (next.find("完整放入画面") != std::string::npos) return std::string("Show full board");
      if (next.find("基础样本") != std::string::npos) return std::string("Collect coarse views");
      if (next.find("覆盖新的画面区域") != std::string::npos)
        return std::string("Move board to a new image area");
      if (next.find("左倾斜") != std::string::npos) return std::string("Tilt board LEFT");
      if (next.find("右倾斜") != std::string::npos) return std::string("Tilt board RIGHT");
      if (next.find("上倾斜") != std::string::npos) return std::string("Tilt board UP");
      if (next.find("下倾斜") != std::string::npos) return std::string("Tilt board DOWN");
      if (next.find("移近") != std::string::npos) return std::string("Move board CLOSER");
      if (next.find("中距离") != std::string::npos) return std::string("Move board to MID");
      if (next.find("移远") != std::string::npos) return std::string("Move board FARTHER");
      if (next.find("采集完成") != std::string::npos) return std::string("Capture complete");
      return std::string("See terminal");
    }();
    cv::putText(display, "Next: " + preview_instruction, cv::Point(20, 68),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    cv::putText(display,
                std::string("Analysis: ") + RejectReasonName(analysis.reason) +
                    "  area=" + std::to_string(analysis.area) +
                    "  blur=" + std::to_string(static_cast<int>(analysis.blur)),
                cv::Point(20, 108), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(180, 220, 255), 2);
    cv::imshow("Camera Calibration - press q to quit", display);
    const int key = cv::waitKey(10);
    if (key == 'q' || key == 'Q' || key == 27) {
      cv::destroyWindow("Camera Calibration - press q to quit");
      return true;
    }
  } catch (const cv::Exception& error) {
    if (!warning_printed) {
      std::cerr << "preview unavailable: " << error.what() << "; continuing in CLI mode\n";
      warning_printed = true;
    }
    preview_disabled = true;
  }
  return false;
}

bool CaptureComplete(const Options& options, const std::vector<AcceptedFrame>& candidates) {
  return EvaluateCapture(options, candidates).complete;
}

bool Calibrate(const Options& options, std::vector<AcceptedFrame> accepted) {
  if (accepted.size() < 10) {
    std::cerr << "FAIL: need at least 10 accepted samples, got " << accepted.size() << "\n";
    return false;
  }
  const cv::Size image_size = accepted.front().image.size();
  if (std::any_of(accepted.begin(), accepted.end(), [&image_size](const AcceptedFrame& frame) {
        return frame.image.size() != image_size;
      })) {
    std::cerr << "FAIL: accepted calibration images do not have a uniform resolution\n";
    return false;
  }
  const std::size_t candidate_count = accepted.size();
  auto provisional = SolveCalibration(options, &accepted);
  if (!std::isfinite(provisional.rms)) {
    std::cerr << "FAIL_PROVISIONAL_CALIBRATION\n";
    return false;
  }
  accepted = SelectKeyframes(options, std::move(accepted),
                             std::min(candidate_count, static_cast<std::size_t>(options.frames)));
  auto solution = SolveCalibration(options, &accepted);
  cv::Mat camera_matrix = solution.camera_matrix;
  cv::Mat distortion = solution.distortion;
  double rms = solution.rms;
  std::vector<double> per_view = solution.per_view;
  std::size_t removed_count = 0U;
  const std::size_t max_removed = accepted.size() / 5U;
  for (int iteration = 0; iteration < 6 && accepted.size() > 20U && removed_count < max_removed;
       ++iteration) {
    const double median = Median(per_view);
    std::vector<double> deviations;
    for (const double value : per_view) deviations.push_back(std::abs(value - median));
    const double threshold = std::max(0.75, median + 3.0 * 1.4826 * Median(deviations));
    const auto worst = std::max_element(per_view.begin(), per_view.end());
    if (worst == per_view.end() || *worst <= threshold) break;
    const std::size_t index = static_cast<std::size_t>(worst - per_view.begin());
    auto trial = accepted;
    trial.erase(trial.begin() + static_cast<std::ptrdiff_t>(index));
    auto trial_solution = SolveCalibration(options, &trial);
    const auto mean = [](const std::vector<double>& values) {
      return std::accumulate(values.begin(), values.end(), 0.0) /
             static_cast<double>(values.size());
    };
    if (!std::isfinite(trial_solution.rms) || !safeToRemove(options, trial) ||
        mean(per_view) - mean(trial_solution.per_view) < 0.01)
      break;
    accepted = std::move(trial);
    solution = std::move(trial_solution);
    camera_matrix = solution.camera_matrix;
    distortion = solution.distortion;
    rms = solution.rms;
    per_view = solution.per_view;
    ++removed_count;
  }
  const double error_sum = std::accumulate(per_view.begin(), per_view.end(), 0.0);
  const double max_error = *std::max_element(per_view.begin(), per_view.end());
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
  const CoverageState coverage = BuildCoverage(options, accepted);
  const int spatial_coverage = coverage.spatial_cells;
  std::vector<std::string> pose_buckets;
  std::vector<std::string> distance_buckets;
  for (const auto& frame : accepted) {
    pose_buckets.push_back(PoseBucket(frame, options));
    distance_buckets.push_back(DistanceBucket(frame, options));
  }
  const bool pose_coverage = coverage.pose_complete();
  const bool distance_coverage = coverage.distance_complete();
  const double outlier_limit = std::max(2.0, median_error + 3.0 * 1.4826 * mad_error);
  for (auto& frame : accepted) frame.outlier = frame.reprojection_error_px > outlier_limit;
  const bool parameter_sane =
      std::isfinite(camera_matrix.at<double>(0, 0)) && camera_matrix.at<double>(0, 0) > 0.0 &&
      camera_matrix.at<double>(1, 1) > 0.0 && camera_matrix.at<double>(0, 2) >= 0.0 &&
      camera_matrix.at<double>(0, 2) <= image_size.width && camera_matrix.at<double>(1, 2) >= 0.0 &&
      camera_matrix.at<double>(1, 2) <= image_size.height;
  const bool coverage_required = !options.board_profile.empty();
  std::string failure_reason = "PASS";
  if (accepted.size() < 20)
    failure_reason = "FAIL_INSUFFICIENT_SAMPLES";
  else if (!parameter_sane)
    failure_reason = "FAIL_PARAMETER_SANITY";
  else if (coverage_required && spatial_coverage < 5)
    failure_reason = "FAIL_SPATIAL_COVERAGE";
  else if (coverage_required && !FinalValidator(options, coverage)) {
    if (!coverage.pose_complete())
      failure_reason = "FAIL_POSE_DIVERSITY";
    else
      failure_reason = "FAIL_DISTANCE_DIVERSITY";
  } else if (rms >= 1.0 || mean_error >= 1.0 || p95_error >= 1.5 || max_error >= 2.0)
    failure_reason = "FAIL_REPROJECTION_ERROR";
  const bool pass = failure_reason == "PASS";
  if (!pass) std::cerr << failure_reason << '\n';
  std::filesystem::create_directories(options.output_dir);
  const auto yaml_path = options.output_dir / "calibration_result.yaml";
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
  csv << "view,sequence,selected,outlier,blur,mean_gray,area,grid_coverage,center_x,center_y,"
         "horizontal_tilt_deg,vertical_tilt_deg,in_plane_rotation_deg,distance_m,pose_bucket,"
         "distance_bucket,reprojection_error_px\n";
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    csv << index << ',' << accepted[index].sequence << ',' << accepted[index].selected << ','
        << accepted[index].outlier << ',' << accepted[index].blur << ',' << accepted[index].mean
        << ',' << accepted[index].area << ',' << accepted[index].grid << ','
        << accepted[index].center_x_normalized << ',' << accepted[index].center_y_normalized << ','
        << accepted[index].horizontal_tilt_deg << ',' << accepted[index].vertical_tilt_deg << ','
        << accepted[index].in_plane_rotation_deg << ',' << accepted[index].distance_m << ','
        << pose_buckets[index] << ',' << distance_buckets[index] << ',' << per_view[index] << '\n';
  }
  cv::Mat undistorted;
  cv::undistort(accepted.front().image, undistorted, camera_matrix, distortion);
  cv::imwrite(original_preview_path.string(), accepted.front().image);
  cv::imwrite(preview_path.string(), undistorted);
  WriteJson(json_path, options, image_size, accepted, rms, mean_error, median_error, mad_error,
            p95_error, max_error, spatial_coverage, pose_coverage, distance_coverage,
            candidate_count, removed_count, failure_reason, pass);
  std::cout << std::fixed << std::setprecision(4) << "candidate_samples=" << candidate_count
            << " selected_samples=" << accepted.size() << " removed_outliers=" << removed_count
            << "\n"
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
            << "status=" << failure_reason << "\n"
            << "yaml=" << yaml_path << "\n"
            << "json=" << json_path << "\n"
            << "csv=" << csv_path << "\n"
            << "original_preview=" << original_preview_path << "\n"
            << "undistorted_preview=" << preview_path << "\n";
  return pass;
}

int RunImpl(int argc, char** argv) {
  Options options;
  const ParseResult parse_result = ParseOptions(argc, argv, &options);
  if (parse_result == ParseResult::kHelp) return 0;
  if (parse_result == ParseResult::kError) return 2;
  std::vector<AcceptedFrame> accepted;
  const std::size_t candidate_goal = static_cast<std::size_t>(
      std::min(options.max_candidates, std::max(options.frames + 10, options.frames)));
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
      auto analyzed = Analyze(image, accepted.size() + 1, options);
      if (analyzed.frame.has_value()) accepted.push_back(std::move(*analyzed.frame));
      if (accepted.size() >= candidate_goal) break;
    }
  } else {
    cockpit::camera::ArgusIspPreviewSource pipeline;
    std::mutex mutex;
    std::condition_variable condition;
    bool capture_complete = false;
    cv::Mat latest_image;
    std::uint64_t latest_sequence = 0;
    bool first_frame_logged = false;
    cv::Mat analysis_image;
    std::uint64_t analysis_sequence = 0;
    bool analysis_pending = false;
    bool analysis_stop = false;
    const std::uint64_t analysis_period_frames =
        std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(options.fps) / 5U);
    AnalyzeResult latest_analysis;
    AnalysisStats analysis_stats;
    CaptureEvaluation evaluation;
    evaluation.guidance = "请将棋盘完整放入画面并保持清晰";
    bool evaluation_valid = false;
    std::string error;
    const bool started = pipeline.Start(
        cockpit::camera::CameraPreviewConfig{
            options.device, static_cast<std::uint32_t>(options.width),
            static_cast<std::uint32_t>(options.height), static_cast<std::uint32_t>(options.fps),
            cockpit::camera::CameraPixelFormat::kBgrx},
        [&](cockpit::camera::CameraFrame frame) {
          cv::Mat image = ToBgr(frame);
          {
            std::lock_guard<std::mutex> lock(mutex);
            latest_image = image.clone();
            latest_sequence = frame.sequence;
            if (frame.sequence % analysis_period_frames == 0U) {
              analysis_image = image;
              analysis_sequence = frame.sequence;
              analysis_pending = true;
            }
            if (!first_frame_logged) {
              std::cerr << "camera frame callback: " << image.cols << 'x' << image.rows
                        << " type=" << image.type() << "\n";
              first_frame_logged = true;
            }
          }
          condition.notify_one();
        },
        &error);
    if (!started) {
      std::cerr << "capture failed: " << (error.empty() ? "unknown error" : error) << '\n';
      return 1;
    }
    std::thread analysis_thread([&] {
      while (true) {
        cv::Mat image;
        std::uint64_t sequence = 0;
        {
          std::unique_lock<std::mutex> lock(mutex);
          condition.wait(lock, [&] {
            return analysis_pending || analysis_stop;
          });
          if (!analysis_pending && analysis_stop) return;
          image = analysis_image;
          sequence = analysis_sequence;
          analysis_pending = false;
        }
        auto analyzed = Analyze(image, sequence, options);
        std::vector<AcceptedFrame> updated_candidates;
        {
          std::lock_guard<std::mutex> lock(mutex);
          latest_analysis = analyzed;
          ++analysis_stats.analyzed;
          if (analyzed.reason == RejectReason::kAccepted && analyzed.frame.has_value()) {
            accepted.push_back(std::move(*analyzed.frame));
            ++analysis_stats.accepted;
            updated_candidates = accepted;
          } else {
            ++analysis_stats.rejected[RejectReasonIndex(analyzed.reason)];
          }
        }
        if (!updated_candidates.empty()) {
          const CaptureEvaluation updated_evaluation = EvaluateCapture(options, updated_candidates);
          std::lock_guard<std::mutex> lock(mutex);
          evaluation = updated_evaluation;
          evaluation_valid = true;
          capture_complete = updated_evaluation.complete ||
                             accepted.size() >= static_cast<std::size_t>(options.max_candidates);
          std::cout << "候选数=" << updated_candidates.size() << "\n";
        }
      }
    });
    if (options.preview) {
      try {
        setenv("GTK_MODULES", "", 1);
        cv::namedWindow("Camera Calibration - press q to quit", cv::WINDOW_NORMAL);
        cv::resizeWindow("Camera Calibration - press q to quit", 960, 540);
        std::cout << "\n=== 相机标定预览已打开 ===\n"
                     "请按终端提示移动棋盘，按 q 或 Esc 退出。\n";
        cv::Mat waiting(540, 960, CV_8UC3, cv::Scalar(24, 24, 24));
        cv::putText(waiting, "Waiting for camera frame...", cv::Point(20, 42),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(255, 255, 255), 2);
        cv::imshow("Camera Calibration - press q to quit", waiting);
        cv::waitKey(30);
      } catch (const cv::Exception& preview_error) {
        std::cerr << "预览窗口不可用：" << preview_error.what() << "；继续使用终端模式\n";
      }
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(options.timeout_seconds);
    bool preview_aborted = false;
    auto last_status = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::string preview_next = "Show full checkerboard in camera view";
    std::cout << "下一步：请将 Q12-70-5 棋盘完整放入画面，保持清晰并缓慢移动\n" << std::flush;
    {
      while (true) {
        std::vector<AcceptedFrame> preview_candidates;
        cv::Mat preview_image;
        std::uint64_t preview_sequence = 0;
        AnalyzeResult preview_analysis;
        AnalysisStats preview_stats;
        CaptureEvaluation preview_evaluation;
        {
          std::unique_lock<std::mutex> lock(mutex);
          if (capture_complete || std::chrono::steady_clock::now() >= deadline) break;
          condition.wait_for(lock, std::chrono::milliseconds(50));
          preview_image = latest_image.clone();
          preview_candidates = accepted;
          preview_sequence = latest_sequence;
          preview_analysis = latest_analysis;
          preview_stats = analysis_stats;
          preview_evaluation = evaluation;
        }
        if (preview_evaluation.complete) {
          std::lock_guard<std::mutex> lock(mutex);
          capture_complete = true;
        }
        if (std::chrono::steady_clock::now() - last_status >= std::chrono::seconds(1)) {
          preview_next = preview_evaluation.guidance;
          std::cout << "状态：候选数=" << preview_candidates.size() << "，下一步：" << preview_next
                    << "，最近分析=" << RejectReasonName(preview_analysis.reason)
                    << "，area=" << preview_analysis.area << "，blur=" << preview_analysis.blur
                    << "，累计分析=" << preview_stats.analyzed
                    << "，拒绝(no_board/blur/exposure/area_small/area_large/grid)="
                    << preview_stats.rejected[RejectReasonIndex(RejectReason::kNoChessboard)] << '/'
                    << preview_stats.rejected[RejectReasonIndex(RejectReason::kBlur)] << '/'
                    << preview_stats.rejected[RejectReasonIndex(RejectReason::kExposure)] << '/'
                    << preview_stats.rejected[RejectReasonIndex(RejectReason::kAreaTooSmall)] << '/'
                    << preview_stats.rejected[RejectReasonIndex(RejectReason::kAreaTooLarge)] << '/'
                    << preview_stats.rejected[RejectReasonIndex(RejectReason::kGrid)] << "\n";
          last_status = std::chrono::steady_clock::now();
        }
        if (ShowPreview(options, preview_image, preview_candidates, preview_sequence, preview_next,
                        preview_analysis)) {
          preview_aborted = true;
          break;
        }
      }
    }
    pipeline.Stop();
    {
      std::lock_guard<std::mutex> lock(mutex);
      analysis_stop = true;
    }
    condition.notify_one();
    analysis_thread.join();
    {
      std::lock_guard<std::mutex> lock(mutex);
      capture_complete = (evaluation_valid && evaluation.complete) ||
                         accepted.size() >= static_cast<std::size_t>(options.max_candidates);
    }
    if (preview_aborted) {
      std::cerr << "用户退出预览，采集已停止\n";
      return 1;
    }
    if (!capture_complete) {
      std::cerr << "采集超时：候选数=" << accepted.size() << "，下一步：" << evaluation.guidance
                << "\n";
      return 1;
    }
  }
  if (options.preview) cv::destroyAllWindows();
  return Calibrate(options, accepted) ? 0 : 1;
}

int Run(int argc, char** argv) {
  return RunImpl(argc, argv);
}

}  // namespace cockpit::camera_calibrator
