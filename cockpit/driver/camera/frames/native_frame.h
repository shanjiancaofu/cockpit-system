#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "cockpit/driver/camera/frames/camera_frame.h"
#include "cockpit/driver/camera/tuning/camera_tuning.h"

namespace cockpit::camera {

// All handles are borrowed. They are valid only while the corresponding lease lives.
// The NV12 surface is block-linear; its FD is not a pointer to linear CPU pixels.
struct NativeFrame {
  std::uint64_t session_id = 0;
  std::uint32_t buffer_index = 0;
  int dmabuf_fd = -1;
  std::uint32_t width = 0, height = 0;
  CameraPixelFormat format = CameraPixelFormat::kNv12;
  std::uint32_t capture_id = 0;
  // Argus sensor domain. No assumption that it equals Linux CLOCK_MONOTONIC.
  std::uint64_t sensor_timestamp_ns = 0;
  std::uint64_t frame_duration_ns = 0;
  CameraTimestampClock source_clock = CameraTimestampClock::kUnknown;
  std::int64_t received_at_ns = 0;  // CLOCK_REALTIME / Unix epoch.
  std::int64_t received_steady_ns = 0;
  CameraTuningMetadata tuning;
};

// One owner, movable between threads. Destruction/Reset relinquishes the buffer once.
// Consumers must finish CPU/device access before Reset (SYNC_TYPE_NONE).
// A lease keeps its capture session and native storage alive, even past runtime destruction.
class FrameLease final {
 public:
  FrameLease();
  // For capture backends: return_buffer must not throw and must retain its resource owner.
  FrameLease(NativeFrame frame, std::function<void()> return_buffer);
  ~FrameLease();
  FrameLease(FrameLease&&) noexcept;
  FrameLease& operator=(FrameLease&&) noexcept;
  FrameLease(const FrameLease&) = delete;
  FrameLease& operator=(const FrameLease&) = delete;

  explicit operator bool() const noexcept;
  const NativeFrame& frame() const;
  void Reset() noexcept;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace cockpit::camera
