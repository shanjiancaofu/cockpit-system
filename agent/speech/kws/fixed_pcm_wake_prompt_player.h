#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include "agent/audio/audio_playback_transport.h"
#include "agent/speech/kws/wake_prompt_player.h"

namespace cockpit {
namespace agent {

class FixedPcmWakePromptPlayer final : public WakePromptPlayer {
 public:
  explicit FixedPcmWakePromptPlayer(std::unique_ptr<voice::AudioPlaybackTransport> transport);

  bool Play(std::string* error) override;
  void Stop() override;

 private:
  const std::unique_ptr<voice::AudioPlaybackTransport> transport_;
  std::mutex mutex_;
  std::uint64_t active_playback_id_ = 0;
  std::uint64_t next_playback_id_ = 1;
  std::chrono::milliseconds timeout_{std::chrono::seconds(2)};
  std::atomic_bool stopping_{false};
};

}  // namespace agent
}  // namespace cockpit
