#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "cockpit/modules/audio/wav/wav_file.h"
#include "cockpit/modules/voice/responses/voice_response_sink.h"

namespace cockpit {
namespace voice {

enum class AudioPlaybackSubmitStatus {
  kAccepted,
  kRejected,
  kFailed,
  kCancelled,
};

struct AudioPlaybackSubmitResult {
  AudioPlaybackSubmitStatus status = AudioPlaybackSubmitStatus::kFailed;
  std::string error;
};

enum class AudioPlaybackWaitStatus {
  kCompleted,
  kFailed,
  kCancelled,
  kDropped,
  kTimeout,
  kNotFound,
  kTransportError,
};

struct AudioPlaybackWaitResult {
  AudioPlaybackWaitStatus status = AudioPlaybackWaitStatus::kTransportError;
  std::string error;
};

class AudioPlaybackTransport {
 public:
  virtual ~AudioPlaybackTransport() = default;

  virtual AudioPlaybackSubmitResult Submit(std::uint64_t playback_id,
                                           const audio::PcmBuffer& audio) = 0;
  virtual AudioPlaybackWaitResult Wait(std::uint64_t playback_id,
                                       std::chrono::milliseconds timeout) = 0;
  virtual bool Cancel(std::uint64_t playback_id) = 0;
};

}  // namespace voice
}  // namespace cockpit
