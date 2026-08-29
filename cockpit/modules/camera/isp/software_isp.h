#pragma once

#include <chrono>
#include <cstdint>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "cockpit/modules/camera/frames/camera_frame.h"

namespace cockpit::camera {

enum class BayerPattern {
  kRggb,
};

struct RawBayerFrame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t bytes_per_line = 0;
  std::uint32_t bytes_used = 0;
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ms = 0;
  std::int64_t source_timestamp_ns = 0;
  std::int64_t received_at_ns = 0;
  CameraTimestampClock source_clock = CameraTimestampClock::kUnknown;
  std::uint32_t source_timestamp_flags = 0;
  bool source_timestamp_valid = false;
  BayerPattern pattern = BayerPattern::kRggb;
  std::vector<std::uint8_t> data;
};

struct SoftwareIspTimingMs {
  double raw_unpack = 0.0;
  double normalize = 0.0;
  double demosaic = 0.0;
  double gain_gamma = 0.0;
  double output = 0.0;
  double total = 0.0;
};

struct SoftwareIspConfig {
  std::uint16_t black_level = 64;
  float red_gain = 1.8F;
  float green_gain = 1.0F;
  float blue_gain = 1.5F;
  float gamma = 2.2F;
};

class SoftwareIsp final {
 public:
  explicit SoftwareIsp(SoftwareIspConfig config = {});

  bool Process(const RawBayerFrame& raw, CameraFrame* output, std::string* error,
               SoftwareIspTimingMs* timing = nullptr);

 private:
  SoftwareIspConfig config_;
  cv::Mat blue_lut_;
  cv::Mat green_lut_;
  cv::Mat red_lut_;
  cv::Mat raw16_;
  cv::Mat raw8_;
  cv::Mat bgr_;
  std::vector<cv::Mat> channels_;
};

}  // namespace cockpit::camera
