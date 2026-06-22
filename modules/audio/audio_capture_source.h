#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cockpit {
namespace audio {

enum class CaptureStatus {
  kOk,
  kTimeout,
  kXrunRecovered,
  kStopped,
  kDeviceError,
};

struct CaptureResult {
  CaptureStatus status = CaptureStatus::kDeviceError;
  std::size_t frames_read = 0;
  int device_error = 0;
  std::string message;
};

class AudioCaptureSource {
 public:
  virtual ~AudioCaptureSource() = default;

  virtual bool Open(std::string* error) = 0;
  // frame_capacity counts interleaved PCM frames, not individual samples.
  virtual CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity,
                             int timeout_ms,
                             const std::atomic_bool& stop_requested) = 0;
  virtual void Close() = 0;
};

}  // namespace audio
}  // namespace cockpit
