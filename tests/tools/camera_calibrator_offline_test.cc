#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

namespace {

constexpr int kImageWidth = 1280;
constexpr int kImageHeight = 960;
constexpr int kSquaresX = 12;
constexpr int kSquaresY = 9;
constexpr int kSquarePixels = 64;

const cv::Mat GroundTruthCameraMatrix() {
  return (cv::Mat_<double>(3, 3) << 1000.0, 0.0, 640.0, 0.0, 1020.0, 480.0, 0.0, 0.0, 1.0);
}

bool WriteBoard(const std::filesystem::path& path, int index, bool degenerate = false,
                bool profile = false, bool front_only = false, bool missing_far = false) {
  const int square_pixels = profile ? 128 : kSquarePixels;
  const cv::Size board_size(kSquaresX * square_pixels, kSquaresY * square_pixels);
  cv::Mat board(board_size, CV_8UC1);
  for (int y = 0; y < kSquaresY; ++y) {
    for (int x = 0; x < kSquaresX; ++x) {
      board(cv::Rect(x * square_pixels, y * square_pixels, square_pixels, square_pixels)) =
          ((x + y) & 1) == 0 ? 255 : 0;
    }
  }
  const cv::Mat camera_matrix = GroundTruthCameraMatrix();
  const cv::Mat distortion = cv::Mat::zeros(5, 1, CV_64F);
  const double square_size_m = 0.005;
  std::vector<cv::Point3f> board_corners = {
      {0.0F, 0.0F, 0.0F},
      {static_cast<float>(kSquaresX * square_size_m), 0.0F, 0.0F},
      {static_cast<float>(kSquaresX * square_size_m), static_cast<float>(kSquaresY * square_size_m),
       0.0F},
      {0.0F, static_cast<float>(kSquaresY * square_size_m), 0.0F},
  };
  cv::Vec3d rvec;
  cv::Vec3d tvec;
  if (degenerate) {
    rvec = {0.0, 0.0, 0.0};
    tvec = {-0.035 + 0.003 * (index % 8), -0.025 + 0.003 * ((index * 3) % 8), 0.22};
  } else {
    rvec = {(-0.18 + 0.06 * (index % 7)), (-0.20 + 0.07 * ((index * 3) % 7)),
            (-0.06 + 0.02 * (index % 5))};
    tvec = {-0.025 + 0.012 * (index % 6), -0.020 + 0.009 * ((index * 2) % 6),
            0.18 + 0.015 * (index % 8)};
    if (profile) {
      switch (front_only ? 0 : index % 5) {
        case 0:
          rvec = {0.0, 0.0, 0.0};
          break;
        case 1:
          rvec = {0.0, 0.22, 0.0};
          break;
        case 2:
          rvec = {0.0, -0.22, 0.0};
          break;
        case 3:
          rvec = {0.22, 0.0, 0.0};
          break;
        default:
          rvec = {-0.22, 0.0, 0.0};
          break;
      }
      tvec[2] = 0.14 + 0.04 * (index % (missing_far ? 2 : 3));
      const int position = (index / 5) % 9;
      const double center_x = 300.0 + 340.0 * (position % 3);
      const double center_y = 220.0 + 260.0 * (position / 3);
      tvec[0] = (center_x - 640.0) * tvec[2] / 1000.0 - 0.03;
      tvec[1] = (center_y - 480.0) * tvec[2] / 1020.0 - 0.0225;
    }
  }
  std::vector<cv::Point2f> projected;
  cv::projectPoints(board_corners, rvec, tvec, camera_matrix, distortion, projected);
  const std::vector<cv::Point2f> source = {
      {0.0F, 0.0F},
      {static_cast<float>(board.cols - 1), 0.0F},
      {static_cast<float>(board.cols - 1), static_cast<float>(board.rows - 1)},
      {0.0F, static_cast<float>(board.rows - 1)}};
  const cv::Mat homography = cv::getPerspectiveTransform(source, projected);
  cv::Mat image(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(127));
  cv::warpPerspective(board, image, homography, image.size(), cv::INTER_NEAREST,
                      cv::BORDER_CONSTANT, cv::Scalar(127));
  return cv::imwrite(path.string(), image);
}

bool CheckRecoveredCalibration(const std::filesystem::path& path) {
  cv::FileStorage storage(path.string(), cv::FileStorage::READ);
  if (!storage.isOpened()) return false;
  const cv::Mat truth = GroundTruthCameraMatrix();
  double fx = 0.0;
  double fy = 0.0;
  double cx = 0.0;
  double cy = 0.0;
  double k1 = 0.0;
  double k2 = 0.0;
  double p1 = 0.0;
  double p2 = 0.0;
  double k3 = 0.0;
  storage["fx"] >> fx;
  storage["fy"] >> fy;
  storage["cx"] >> cx;
  storage["cy"] >> cy;
  storage["k1"] >> k1;
  storage["k2"] >> k2;
  storage["p1"] >> p1;
  storage["p2"] >> p2;
  storage["k3"] >> k3;
  const double rms = static_cast<double>(storage["rms"]);
  return std::abs(fx - truth.at<double>(0, 0)) < 20.0 &&
         std::abs(fy - truth.at<double>(1, 1)) < 20.0 &&
         std::abs(cx - truth.at<double>(0, 2)) < 12.0 &&
         std::abs(cy - truth.at<double>(1, 2)) < 12.0 && std::abs(k1) < 0.05 &&
         std::abs(k2) < 0.15 && std::abs(p1) < 0.02 && std::abs(p2) < 0.02 && std::abs(k3) < 0.15 &&
         std::isfinite(rms) && rms < 1.0;
}

int Run(const std::string& calibrator, const std::filesystem::path& input_dir,
        const std::filesystem::path& output_dir, const std::string& extra = {}) {
  const std::string command = "\"" + calibrator + "\" --input-dir \"" + input_dir.string() +
                              "\" --output-dir \"" + output_dir.string() +
                              "\" --corners-x 11 --corners-y 8 --square-size 0.005 " + extra +
                              " 2>&1";
  return std::system(command.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::filesystem::path root =
      std::filesystem::path("/tmp") / ("cockpit-camera-calibration-" + std::to_string(getpid()));
  const auto input_dir = root / "input";
  const auto output_dir = root / "output";
  std::filesystem::create_directories(input_dir);
  for (int index = 0; index < 24; ++index) {
    if (!WriteBoard(input_dir / ("frame-" + std::to_string(index) + ".png"), index)) return 1;
  }
  const int success =
      Run(argv[1], input_dir, output_dir,
          "--frames 20 --max-candidates 30 --duplicate-threshold 0 --grid-required 1 --blur-min 0");
  const bool artifacts = std::filesystem::exists(output_dir / "calibration_result.yaml") &&
                         std::filesystem::exists(output_dir / "calibration_report.json") &&
                         std::filesystem::exists(output_dir / "per_view_errors.csv") &&
                         std::filesystem::exists(output_dir / "undistorted_preview.jpg");
  const bool recovered = CheckRecoveredCalibration(output_dir / "calibration_result.yaml");
  std::ifstream report_stream(output_dir / "calibration_report.json");
  const std::string report((std::istreambuf_iterator<char>(report_stream)),
                           std::istreambuf_iterator<char>());
  const bool selection_reported = report.find("\"candidate_samples\": ") != std::string::npos &&
                                  report.find("\"selected_samples\": 20") != std::string::npos &&
                                  report.find("\"failure_reason\": \"PASS\"") != std::string::npos;
  const int insufficient = Run(argv[1], input_dir, root / "insufficient", "--frames 5");
  const int duplicate = Run(argv[1], input_dir, root / "duplicate");
  std::filesystem::copy_file(input_dir / "frame-0.png", input_dir / "mixed.png",
                             std::filesystem::copy_options::overwrite_existing);
  cv::Mat mixed = cv::imread((input_dir / "mixed.png").string(), cv::IMREAD_GRAYSCALE);
  cv::resize(mixed, mixed, cv::Size(320, 240));
  cv::imwrite((input_dir / "mixed.png").string(), mixed);
  const int mixed_result = Run(argv[1], input_dir, root / "mixed");
  const auto degenerate_dir = root / "degenerate";
  std::filesystem::create_directories(degenerate_dir);
  for (int index = 0; index < 24; ++index) {
    if (!WriteBoard(degenerate_dir / ("frame-" + std::to_string(index) + ".png"), index, true))
      return 1;
  }
  const int degenerate_result =
      Run(argv[1], degenerate_dir, root / "degenerate-output",
          "--board-profile q12-70-5 --frames 20 --max-candidates 24 --duplicate-threshold 0 "
          "--grid-required 1 --blur-min 0");
  const auto profile_dir = root / "q12-good";
  std::filesystem::create_directories(profile_dir);
  for (int index = 0; index < 90; ++index) {
    if (!WriteBoard(profile_dir / ("frame-" + std::to_string(index) + ".png"), index, false, true))
      return 1;
  }
  const auto profile_output = root / "q12-good-output";
  const int profile_result =
      Run(argv[1], profile_dir, profile_output,
          "--board-profile q12-70-5 --frames 20 --max-candidates 40 --duplicate-threshold 0 "
          "--blur-min 0 --near-distance 0.16 --far-distance 0.20 --tilt-threshold 8");
  std::ifstream profile_report_stream(profile_output / "calibration_report.json");
  const std::string profile_report((std::istreambuf_iterator<char>(profile_report_stream)),
                                   std::istreambuf_iterator<char>());
  const bool profile_passed =
      profile_report.find("\"failure_reason\": \"PASS\"") != std::string::npos &&
      profile_report.find("\"pose_coverage\": true") != std::string::npos &&
      profile_report.find("\"distance_coverage\": true") != std::string::npos;
  const auto front_only_dir = root / "q12-front-only";
  std::filesystem::create_directories(front_only_dir);
  for (int index = 0; index < 30; ++index) {
    if (!WriteBoard(front_only_dir / ("frame-" + std::to_string(index) + ".png"), index, false,
                    true, true))
      return 1;
  }
  const auto front_only_output = root / "q12-front-only-output";
  const int front_only_result =
      Run(argv[1], front_only_dir, front_only_output,
          "--board-profile q12-70-5 --frames 20 --max-candidates 30 --duplicate-threshold 0 "
          "--blur-min 0 --near-distance 0.16 --far-distance 0.20 --tilt-threshold 8");
  std::ifstream front_only_report_stream(front_only_output / "calibration_report.json");
  const std::string front_only_report((std::istreambuf_iterator<char>(front_only_report_stream)),
                                      std::istreambuf_iterator<char>());
  const bool front_only_failed =
      front_only_result != 0 &&
      front_only_report.find("\"failure_reason\": \"FAIL_POSE_DIVERSITY\"") != std::string::npos;
  const auto missing_far_dir = root / "q12-missing-far";
  std::filesystem::create_directories(missing_far_dir);
  for (int index = 0; index < 30; ++index) {
    if (!WriteBoard(missing_far_dir / ("frame-" + std::to_string(index) + ".png"), index, false,
                    true, false, true))
      return 1;
  }
  const auto missing_far_output = root / "q12-missing-far-output";
  const int missing_far_result =
      Run(argv[1], missing_far_dir, missing_far_output,
          "--board-profile q12-70-5 --frames 20 --max-candidates 30 --duplicate-threshold 0 "
          "--blur-min 0 --near-distance 0.16 --far-distance 0.80 --tilt-threshold 8");
  std::ifstream missing_far_report_stream(missing_far_output / "calibration_report.json");
  const std::string missing_far_report((std::istreambuf_iterator<char>(missing_far_report_stream)),
                                       std::istreambuf_iterator<char>());
  const bool missing_far_failed =
      missing_far_result != 0 &&
      missing_far_report.find("\"failure_reason\": \"FAIL_DISTANCE_DIVERSITY\"") !=
          std::string::npos;
  std::filesystem::remove_all(root);
  return success == 0 && artifacts && recovered && selection_reported && insufficient != 0 &&
                 duplicate != 0 && mixed_result != 0 && degenerate_result != 0 &&
                 profile_result == 0 && profile_passed && front_only_failed && missing_far_failed
             ? 0
             : 1;
}
