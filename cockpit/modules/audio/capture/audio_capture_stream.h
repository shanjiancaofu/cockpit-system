#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/audio/capture/audio_capture_source.h"
#include "cockpit/modules/audio/frames/audio_frame.h"
#include "cockpit/modules/audio/frames/spsc_ring_buffer.h"

namespace cockpit {
namespace audio {

enum class AudioCaptureState {
  kStopped,
  kStarting,
  kRunning,
  kRecovering,
  kFaulted,
};

struct AudioCaptureMetrics {
  std::uint64_t pcm_frames_read = 0;
  std::uint64_t audio_frames_published = 0;
  std::uint64_t audio_frames_dropped = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t xruns = 0;
  std::uint64_t device_errors = 0;
};

class AudioCaptureStream {
 public:
  static constexpr std::size_t kBufferCapacity = 64;

  explicit AudioCaptureStream(std::unique_ptr<AudioCaptureSource> source);
  ~AudioCaptureStream();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioCaptureStream);

  bool Start(std::string* error = nullptr);
  void Stop();
  std::optional<AudioFrame> TryPop();

  AudioCaptureState state() const {
    return state_.load();
  }
  AudioCaptureMetrics metrics() const;
  std::string last_error() const;

 private:
  void Run();
  void SetError(std::string error);
  void ResetMetrics();

  std::unique_ptr<AudioCaptureSource> source_;
  SpscRingBuffer<AudioFrame, kBufferCapacity> buffer_;
  std::atomic<AudioCaptureState> state_{AudioCaptureState::kStopped};
  std::atomic_bool stop_requested_{false};
  std::thread worker_;

  std::atomic<std::uint64_t> pcm_frames_read_{0};
  std::atomic<std::uint64_t> audio_frames_published_{0};
  std::atomic<std::uint64_t> timeouts_{0};
  std::atomic<std::uint64_t> xruns_{0};
  std::atomic<std::uint64_t> device_errors_{0};

  mutable std::mutex error_mutex_;
  std::string last_error_;
};

}  // namespace audio
}  // namespace cockpit
