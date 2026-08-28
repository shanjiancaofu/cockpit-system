#include "cockpit/modules/camera/isp/software_isp.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <opencv2/imgproc.hpp>

namespace cockpit::camera {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

bool ValidConfig(const SoftwareIspConfig& config) {
  return std::isfinite(config.red_gain) && std::isfinite(config.green_gain) &&
         std::isfinite(config.blue_gain) && std::isfinite(config.gamma) && config.red_gain > 0.0F &&
         config.green_gain > 0.0F && config.blue_gain > 0.0F && config.gamma > 0.0F &&
         config.black_level < 1023U;
}

cv::Mat BuildLut(float gain, float gamma) {
  cv::Mat lut(1, 256, CV_8UC1);
  const float inverse_gamma = 1.0F / gamma;
  for (int value = 0; value < 256; ++value) {
    const float normalized = std::clamp(static_cast<float>(value) / 255.0F * gain, 0.0F, 1.0F);
    lut.at<std::uint8_t>(0, value) = static_cast<std::uint8_t>(
        std::clamp(std::lround(std::pow(normalized, inverse_gamma) * 255.0F), 0L, 255L));
  }
  return lut;
}

}  // namespace

SoftwareIsp::SoftwareIsp(SoftwareIspConfig config)
    : config_(config),
      blue_lut_(BuildLut(config.blue_gain, config.gamma)),
      green_lut_(BuildLut(config.green_gain, config.gamma)),
      red_lut_(BuildLut(config.red_gain, config.gamma)) {
}

bool SoftwareIsp::Process(const RawBayerFrame& raw, CameraFrame* output, std::string* error) const {
  const std::size_t minimum_bytes = static_cast<std::size_t>(raw.bytes_per_line) * raw.height;
  if (output == nullptr || raw.width == 0 || raw.height == 0 ||
      raw.bytes_per_line < raw.width * 2U || raw.data.size() < raw.bytes_used ||
      raw.bytes_used < minimum_bytes || !ValidConfig(config_)) {
    SetError(error, "invalid RAW Bayer frame or software ISP configuration");
    return false;
  }
  if (raw.pattern != BayerPattern::kRggb) {
    SetError(error, "unsupported Bayer pattern");
    return false;
  }

  cv::Mat raw16(static_cast<int>(raw.height), static_cast<int>(raw.width), CV_16UC1);
  for (std::uint32_t y = 0; y < raw.height; ++y) {
    const auto* source = raw.data.data() + static_cast<std::size_t>(y) * raw.bytes_per_line;
    auto* destination = raw16.ptr<std::uint16_t>(static_cast<int>(y));
    for (std::uint32_t x = 0; x < raw.width; ++x) {
      const std::size_t offset = static_cast<std::size_t>(x) * 2U;
      const std::uint16_t container = static_cast<std::uint16_t>(source[offset]) |
                                      static_cast<std::uint16_t>(source[offset + 1U]) << 8U;
      destination[x] = static_cast<std::uint16_t>(container >> 6U);
    }
  }

  cv::Mat raw8;
  const double scale = 255.0 / static_cast<double>(1023U - config_.black_level);
  const double offset = -static_cast<double>(config_.black_level) * scale;
  raw16.convertTo(raw8, CV_8UC1, scale, offset);

  cv::Mat bgr;
  cv::cvtColor(raw8, bgr, cv::COLOR_BayerRG2BGR);
  std::vector<cv::Mat> channels;
  cv::split(bgr, channels);
  cv::LUT(channels[0], blue_lut_, channels[0]);
  cv::LUT(channels[1], green_lut_, channels[1]);
  cv::LUT(channels[2], red_lut_, channels[2]);
  cv::merge(channels, bgr);

  cv::Mat bgra;
  cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
  output->sequence = raw.sequence;
  output->timestamp_ms = raw.timestamp_ms;
  output->width = raw.width;
  output->height = raw.height;
  output->stride_bytes = static_cast<std::uint32_t>(bgra.step);
  output->format = CameraPixelFormat::kBgrx;
  output->data.assign(bgra.datastart, bgra.dataend);
  return true;
}

}  // namespace cockpit::camera
