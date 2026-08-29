#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

namespace {

constexpr int kImageWidth = 640;
constexpr int kImageHeight = 480;
constexpr int kSquaresX = 12;
constexpr int kSquaresY = 9;
constexpr int kSquarePixels = 32;

bool WriteBoard(const std::filesystem::path& path, int index) {
  const cv::Size board_size(kSquaresX * kSquarePixels, kSquaresY * kSquarePixels);
  cv::Mat board(board_size, CV_8UC1);
  for (int y = 0; y < kSquaresY; ++y) {
    for (int x = 0; x < kSquaresX; ++x) {
      board(cv::Rect(x * kSquarePixels, y * kSquarePixels, kSquarePixels, kSquarePixels)) =
          ((x + y) & 1) == 0 ? 255 : 0;
    }
  }
  const double scale = 0.72 + 0.015 * (index % 5);
  const double angle = -8.0 + 4.0 * (index % 5);
  cv::Mat transformed;
  cv::resize(board, transformed, cv::Size(), scale, scale, cv::INTER_NEAREST);
  const cv::Point2f center(static_cast<float>(transformed.cols / 2.0),
                           static_cast<float>(transformed.rows / 2.0));
  const cv::Mat rotation = cv::getRotationMatrix2D(center, angle, 1.0);
  cv::warpAffine(transformed, transformed, rotation, transformed.size(), cv::INTER_NEAREST,
                 cv::BORDER_CONSTANT, cv::Scalar(127));
  cv::Mat image(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(127));
  const int x = 24 + (index * 71) % (kImageWidth - transformed.cols - 48);
  const int y = 24 + (index * 43) % (kImageHeight - transformed.rows - 48);
  transformed.copyTo(image(cv::Rect(x, y, transformed.cols, transformed.rows)));
  return cv::imwrite(path.string(), image);
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
  const int insufficient = Run(argv[1], input_dir, root / "insufficient", "--frames 5");
  const int duplicate = Run(argv[1], input_dir, root / "duplicate");
  std::filesystem::copy_file(input_dir / "frame-0.png", input_dir / "mixed.png",
                             std::filesystem::copy_options::overwrite_existing);
  cv::Mat mixed = cv::imread((input_dir / "mixed.png").string(), cv::IMREAD_GRAYSCALE);
  cv::resize(mixed, mixed, cv::Size(320, 240));
  cv::imwrite((input_dir / "mixed.png").string(), mixed);
  const int mixed_result = Run(argv[1], input_dir, root / "mixed");
  std::filesystem::remove_all(root);
  return success == 0 && artifacts && insufficient != 0 && duplicate != 0 && mixed_result != 0 ? 0
                                                                                               : 1;
}
