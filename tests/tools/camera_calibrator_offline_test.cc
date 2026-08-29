#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

bool WriteBoard(const std::filesystem::path& path, int index, bool degenerate = false) {
  const cv::Size board_size(kSquaresX * kSquarePixels, kSquaresY * kSquarePixels);
  cv::Mat board(board_size, CV_8UC1);
  for (int y = 0; y < kSquaresY; ++y) {
    for (int x = 0; x < kSquaresX; ++x) {
      board(cv::Rect(x * kSquarePixels, y * kSquarePixels, kSquarePixels, kSquarePixels)) =
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
    tvec = {-0.025, -0.020, 0.22};
  } else {
    rvec = {(-0.18 + 0.06 * (index % 7)), (-0.20 + 0.07 * ((index * 3) % 7)),
            (-0.06 + 0.02 * (index % 5))};
    tvec = {-0.025 + 0.012 * (index % 6), -0.020 + 0.009 * ((index * 2) % 6),
            0.18 + 0.015 * (index % 8)};
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
      Run(argv[1], input_dir, output_dir, "--duplicate-threshold 0 --grid-required 1 --blur-min 0");
  const bool artifacts = std::filesystem::exists(output_dir / "camera_calibration.yaml") &&
                         std::filesystem::exists(output_dir / "calibration_report.json") &&
                         std::filesystem::exists(output_dir / "per_view_errors.csv") &&
                         std::filesystem::exists(output_dir / "undistorted_preview.jpg");
  const bool recovered = CheckRecoveredCalibration(output_dir / "camera_calibration.yaml");
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
    if (!WriteBoard(degenerate_dir / ("frame-" + std::to_string(index) + ".png"), 0, true))
      return 1;
  }
  const int degenerate_result = Run(argv[1], degenerate_dir, root / "degenerate-output");
  std::filesystem::remove_all(root);
  return success == 0 && artifacts && recovered && insufficient != 0 && duplicate != 0 &&
                 mixed_result != 0 && degenerate_result != 0
             ? 0
             : 1;
}
