#include "cockpit/modules/camera/isp/software_isp.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <opencv2/imgproc.hpp>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

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

bool SoftwareIsp::Process(const RawBayerFrame& raw, CameraFrame* output, std::string* error,
                          SoftwareIspTimingMs* timing) {
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

  const auto total_started = std::chrono::steady_clock::now();
  raw16_.create(static_cast<int>(raw.height), static_cast<int>(raw.width), CV_16UC1);
#if defined(__aarch64__)
  const bool disable_neon = std::getenv("COCKPIT_CAMERA_DISABLE_NEON") != nullptr;
#endif
  for (std::uint32_t y = 0; y < raw.height; ++y) {
    const auto* source = raw.data.data() + static_cast<std::size_t>(y) * raw.bytes_per_line;
    auto* destination = raw16_.ptr<std::uint16_t>(static_cast<int>(y));
    std::uint32_t x = 0;
#if defined(__aarch64__)
    for (; !disable_neon && x + 8U <= raw.width; x += 8U) {
      const auto samples = vld1q_u16(reinterpret_cast<const std::uint16_t*>(source) + x);
      vst1q_u16(destination + x, vshrq_n_u16(samples, 6));
    }
#endif
    for (; x < raw.width; ++x) {
      const std::size_t offset = static_cast<std::size_t>(x) * 2U;
      const std::uint16_t container = static_cast<std::uint16_t>(source[offset]) |
                                      static_cast<std::uint16_t>(source[offset + 1U]) << 8U;
      destination[x] = static_cast<std::uint16_t>(container >> 6U);
    }
  }

  const auto unpack_finished = std::chrono::steady_clock::now();
  const double scale = 255.0 / static_cast<double>(1023U - config_.black_level);
  const double offset = -static_cast<double>(config_.black_level) * scale;
  raw16_.convertTo(raw8_, CV_8UC1, scale, offset);
  const auto normalize_finished = std::chrono::steady_clock::now();

  cv::cvtColor(raw8_, bgr_, cv::COLOR_BayerRG2BGR);
  const auto demosaic_finished = std::chrono::steady_clock::now();
  if (channels_.size() != 3U) channels_.resize(3U);
  cv::split(bgr_, channels_);
  cv::LUT(channels_[0], blue_lut_, channels_[0]);
  cv::LUT(channels_[1], green_lut_, channels_[1]);
  cv::LUT(channels_[2], red_lut_, channels_[2]);
  cv::merge(channels_, bgr_);
  const auto color_finished = std::chrono::steady_clock::now();

  output->sequence = raw.sequence;
  output->timestamp_ms = raw.timestamp_ms;
  output->source_timestamp_ns = raw.source_timestamp_ns;
  output->received_at_ns = raw.received_at_ns;
  output->source_clock = raw.source_clock;
  output->source_timestamp_flags = raw.source_timestamp_flags;
  output->source_timestamp_valid = raw.source_timestamp_valid;
  output->width = raw.width;
  output->height = raw.height;
  output->stride_bytes = raw.width * 4U;
  output->format = CameraPixelFormat::kBgrx;
  const std::size_t output_size = static_cast<std::size_t>(output->stride_bytes) * output->height;
  output->data.resize(output_size);
  cv::Mat output_bgra(static_cast<int>(output->height), static_cast<int>(output->width), CV_8UC4,
                      output->data.data(), output->stride_bytes);
  cv::cvtColor(bgr_, output_bgra, cv::COLOR_BGR2BGRA);
  if (timing != nullptr) {
    const auto milliseconds = [](auto duration) {
      return std::chrono::duration<double, std::milli>(duration).count();
    };
    timing->raw_unpack = milliseconds(unpack_finished - total_started);
    timing->normalize = milliseconds(normalize_finished - unpack_finished);
    timing->demosaic = milliseconds(demosaic_finished - normalize_finished);
    timing->gain_gamma = milliseconds(color_finished - demosaic_finished);
    timing->output = milliseconds(std::chrono::steady_clock::now() - color_finished);
    timing->total = milliseconds(std::chrono::steady_clock::now() - total_started);
  }
  return true;
}

}  // namespace cockpit::camera
