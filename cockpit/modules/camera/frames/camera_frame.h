#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cockpit {
namespace camera {

enum class CameraPixelFormat {
  kUnknown,
  kRgb,
  kBgrx,
  kYuyv,
  kMjpeg,
  kNv12,
};

struct CameraFrame {
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ms = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride_bytes = 0;
  CameraPixelFormat format = CameraPixelFormat::kUnknown;
  std::vector<std::uint8_t> data;

  bool HasValidLayout() const;
  bool HasValidPayloadSize(std::size_t payload_size) const;
  bool IsValid() const;
};

std::string ToString(CameraPixelFormat format);

}  // namespace camera
}  // namespace cockpit
