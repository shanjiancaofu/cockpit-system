#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace audio {

struct AudioStreamPublisherMetrics {
  std::uint64_t clients_accepted = 0;
  std::uint64_t frames_queued = 0;
  std::uint64_t frames_sent = 0;
  std::uint64_t frames_dropped = 0;
  std::uint64_t client_disconnects = 0;
};

class AudioStreamPublisher {
 public:
  explicit AudioStreamPublisher(std::size_t queue_capacity = 64);
  ~AudioStreamPublisher();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioStreamPublisher);

  bool Start(const std::string& socket_path, std::string* error = nullptr);
  void Stop();
  bool Publish(const AudioFrame& frame);
  AudioStreamPublisherMetrics metrics() const;

 private:
  void Run();
  int AcceptClient(int listener_fd);
  bool SendFrame(int client_fd, const AudioFrame& frame);
  void RemoveOwnedSocket();

  const std::size_t queue_capacity_;
  mutable std::mutex mutex_;
  std::condition_variable frame_available_;
  std::deque<AudioFrame> frames_;
  bool discontinuity_pending_{false};
  std::atomic_bool running_{false};
  std::thread worker_;
  int listener_fd_{-1};
  std::string socket_path_;
  std::uint64_t socket_device_{0};
  std::uint64_t socket_inode_{0};
  std::atomic<std::uint64_t> clients_accepted_{0};
  std::atomic<std::uint64_t> frames_queued_{0};
  std::atomic<std::uint64_t> frames_sent_{0};
  std::atomic<std::uint64_t> frames_dropped_{0};
  std::atomic<std::uint64_t> client_disconnects_{0};
};

}  // namespace audio
}  // namespace cockpit
