#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cockpit/driver/camera/frames/frame_broker.h"

namespace cockpit::camera {
enum class VicOutputFormat { kBgrx, kNv12, kI420 };
struct CpuVideoPlane {
  std::size_t offset = 0;
  std::uint32_t stride = 0, row_bytes = 0, height = 0;
};
struct CpuVideoFrame {
  VicOutputFormat format = VicOutputFormat::kBgrx;
  std::uint32_t width = 0, height = 0;
  std::uint64_t session_id = 0;
  std::uint32_t capture_id = 0;
  std::uint64_t sensor_timestamp_ns = 0, frame_duration_ns = 0;
  std::int64_t received_at_ns = 0;
  CameraTimestampClock source_clock = CameraTimestampClock::kUnknown;
  std::vector<CpuVideoPlane> planes;
  std::vector<std::uint8_t> data;
};
struct VicTransformConfig {
  std::uint32_t width = 1280, height = 720;
  VicOutputFormat format = VicOutputFormat::kBgrx;
};
struct VicTransformStats {
  std::uint64_t transformed = 0, failures = 0, allocated = 0, destroyed = 0;
  std::uint64_t mapped = 0, unmapped = 0;
  double total_ms = 0;
  bool ResourcesBalanced() const {
    return allocated == destroyed && mapped == unmapped;
  }
};

// Synchronous VIC -> pitch-linear surface -> synchronized owned CPU copy.
// No async fence, implicit GPU fallback or changes to the native input allocator.
class VicTransform final {
 public:
  VicTransform();
  ~VicTransform();
  VicTransform(const VicTransform&) = delete;
  VicTransform& operator=(const VicTransform&) = delete;
  static bool IsSupported();
  bool Convert(SharedFrameLease input, const VicTransformConfig& config, CpuVideoFrame* output,
               std::string* error = nullptr);
  bool Reset(std::string* error = nullptr);
  VicTransformStats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Compatibility boundary only: moves owned BGRx bytes into the existing CPU frame.
bool TakeCameraFrame(CpuVideoFrame&& input, CameraFrame* output, std::string* error = nullptr);

namespace detail {
struct MappedVideoPlane {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
  std::uint32_t stride = 0;
};
// Shared by SDK implementation and hardware-independent stride/plane tests.
bool CopyMappedVideoPlanes(const std::vector<MappedVideoPlane>& planes,
                           const VicTransformConfig& config, CpuVideoFrame* output,
                           std::string* error);
}  // namespace detail
}  // namespace cockpit::camera
