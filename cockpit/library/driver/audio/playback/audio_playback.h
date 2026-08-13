#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/audio/playback/audio_player.h"

namespace cockpit {
namespace audio {

struct AudioPlaybackMetrics {
  std::uint64_t queued = 0;
  std::uint64_t played = 0;
  std::uint64_t failed = 0;
  std::uint64_t dropped = 0;
};

enum class AudioPlaybackStatus {
  kPending,
  kCompleted,
  kFailed,
  kCancelled,
  kDropped,
};

struct AudioPlaybackResult {
  std::uint64_t playback_id = 0;
  AudioPlaybackStatus status = AudioPlaybackStatus::kPending;
  std::string error;
};

enum class AudioPlaybackWaitStatus {
  kReady,
  kTimeout,
  kNotFound,
};

class AudioPlayback {
 public:
  AudioPlayback(std::string device, std::unique_ptr<AudioPlayer> player);
  ~AudioPlayback();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioPlayback);

  bool Start(std::string* error = nullptr);
  void Stop();
  std::optional<std::uint64_t> Submit(PcmBuffer buffer, std::uint64_t playback_id = 0U);
  bool Cancel(std::uint64_t playback_id);
  AudioPlaybackWaitStatus WaitForResult(std::uint64_t playback_id,
                                        std::chrono::milliseconds timeout,
                                        AudioPlaybackResult* result);
  AudioPlaybackMetrics metrics() const;

 private:
  struct PlaybackRequest;

  void FinalizeLocked(std::uint64_t playback_id, AudioPlaybackStatus status, std::string error);
  void Run();

  static constexpr std::size_t kQueueCapacity = 8U;
  static constexpr std::size_t kResultCapacity = 64U;
  const std::string device_;
  const std::unique_ptr<AudioPlayer> player_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::condition_variable result_changed_;
  std::deque<std::shared_ptr<PlaybackRequest>> queue_;
  std::shared_ptr<PlaybackRequest> active_;
  std::unordered_map<std::uint64_t, AudioPlaybackResult> results_;
  std::deque<std::uint64_t> final_result_ids_;
  bool running_{false};
  std::atomic_bool stop_requested_{false};
  std::uint64_t next_playback_id_ = 1U;
  std::thread worker_;
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> played_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace audio
}  // namespace cockpit
