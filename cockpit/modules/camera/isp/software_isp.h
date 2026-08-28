#pragma once

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
  BayerPattern pattern = BayerPattern::kRggb;
  std::vector<std::uint8_t> data;
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

  bool Process(const RawBayerFrame& raw, CameraFrame* output, std::string* error) const;

 private:
  SoftwareIspConfig config_;
  cv::Mat blue_lut_;
  cv::Mat green_lut_;
  cv::Mat red_lut_;
};

}  // namespace cockpit::camera
