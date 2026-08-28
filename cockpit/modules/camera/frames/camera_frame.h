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
  kSrgGb10,
};

// Identifies the clock domain of source_timestamp_ns. received_at_ns always uses
// the host CLOCK_REALTIME/Unix epoch domain.
enum class CameraTimestampClock : std::uint32_t {
  kUnknown = 0,
  kMonotonic = 1,
  kRealtime = 2,
  kGstreamerRunningTime = 3,
};

struct CameraFrame {
  std::uint64_t sequence = 0;
  // Backward-compatible host receive timestamp in Unix epoch milliseconds.
  std::uint64_t timestamp_ms = 0;
  std::int64_t source_timestamp_ns = 0;
  std::int64_t received_at_ns = 0;
  CameraTimestampClock source_clock = CameraTimestampClock::kUnknown;
  // Optional backend flags, for example V4L2 timestamp/source flag bits.
  std::uint32_t source_timestamp_flags = 0;
  bool source_timestamp_valid = false;
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
std::string ToString(CameraTimestampClock clock);

}  // namespace camera
}  // namespace cockpit
