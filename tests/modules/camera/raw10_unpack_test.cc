#include "cockpit/modules/camera/isp/raw10_unpack.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr std::uint32_t kWidth = 11;
constexpr std::uint32_t kHeight = 2;
constexpr std::uint32_t kSourceStride = 24;
constexpr std::size_t kDestinationStride = 13;
constexpr std::uint16_t kSentinel = 0xFFFFU;

const std::array<std::uint16_t, (kWidth * kHeight)> kSamples = {
    0, 1, 63, 64,  255, 256, 511, 512, 768, 1022, 1023,
    7, 9, 31, 127, 129, 383, 640, 777, 900, 1000, 1010,
};

std::vector<std::uint8_t> BuildSource() {
  std::vector<std::uint8_t> source(static_cast<std::size_t>(kSourceStride) * kHeight, 0xA5U);
  for (std::uint32_t y = 0; y < kHeight; ++y) {
    for (std::uint32_t x = 0; x < kWidth; ++x) {
      const std::uint16_t container =
          static_cast<std::uint16_t>(kSamples[static_cast<std::size_t>(y) * kWidth + x] << 6U);
      const std::size_t offset = static_cast<std::size_t>(y) * kSourceStride + x * 2U;
      source[offset] = static_cast<std::uint8_t>(container & 0xFFU);
      source[offset + 1U] = static_cast<std::uint8_t>(container >> 8U);
    }
  }
  return source;
}

bool CheckOutput(const std::vector<std::uint16_t>& output, const char* mode) {
  for (std::uint32_t y = 0; y < kHeight; ++y) {
    for (std::uint32_t x = 0; x < kWidth; ++x) {
      const std::size_t sample_index = static_cast<std::size_t>(y) * kWidth + x;
      const std::size_t output_index = static_cast<std::size_t>(y) * kDestinationStride + x;
      if (output[output_index] != kSamples[sample_index]) {
        std::cerr << mode << " RAW10 unpack mismatch at " << x << ',' << y << '\n';
        return false;
      }
    }
    for (std::size_t x = kWidth; x < kDestinationStride; ++x) {
      if (output[static_cast<std::size_t>(y) * kDestinationStride + x] != kSentinel) {
        std::cerr << mode << " RAW10 unpack overwrote destination padding\n";
        return false;
      }
    }
  }
  return true;
}

bool Run(const std::vector<std::uint8_t>& source, const char* mode) {
  std::vector<std::uint16_t> output(kDestinationStride * kHeight, kSentinel);
  cockpit::camera::UnpackRaw10(source.data(), kWidth, kHeight, kSourceStride, output.data(),
                               kDestinationStride);
  return CheckOutput(output, mode);
}

}  // namespace

int main() {
  const auto source = BuildSource();
  unsetenv("COCKPIT_CAMERA_DISABLE_NEON");
  if (!Run(source, "default")) {
    return 1;
  }
  setenv("COCKPIT_CAMERA_DISABLE_NEON", "1", 1);
  const bool scalar_ok = Run(source, "scalar");
  unsetenv("COCKPIT_CAMERA_DISABLE_NEON");
  return scalar_ok ? 0 : 1;
}
