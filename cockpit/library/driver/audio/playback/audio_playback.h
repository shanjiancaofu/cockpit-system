#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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

class AudioPlayback {
 public:
  AudioPlayback(std::string device, std::unique_ptr<AudioPlayer> player);
  ~AudioPlayback();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioPlayback);

  bool Start(std::string* error = nullptr);
  void Stop();
  bool Submit(PcmBuffer buffer);
  AudioPlaybackMetrics metrics() const;

 private:
  void Run();

  static constexpr std::size_t kQueueCapacity = 8U;
  const std::string device_;
  const std::unique_ptr<AudioPlayer> player_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<PcmBuffer> queue_;
  bool running_{false};
  std::atomic_bool stop_requested_{false};
  std::thread worker_;
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> played_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace audio
}  // namespace cockpit
