#pragma once

#include "modules/audio/audio_frame.h"

namespace cockpit {
namespace audio {

enum class VoiceActivityState {
  kSilence,
  kSpeech,
};

struct VoiceActivityResult {
  VoiceActivityState state = VoiceActivityState::kSilence;
  double level_dbfs = -120.0;
  bool state_changed = false;
};

class VoiceActivityDetector {
 public:
  virtual ~VoiceActivityDetector() = default;

  virtual VoiceActivityResult Analyze(const AudioFrame& frame) = 0;
  virtual void Reset() = 0;
};

}  // namespace audio
}  // namespace cockpit
