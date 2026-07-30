#include "agent/speech/vad/mock_voice_activity_detector.h"

namespace cockpit {
namespace agent {

audio::VoiceActivityResult MockVoiceActivityDetector::Analyze(const audio::AudioFrame&) {
  const bool changed = !active_;
  active_ = true;
  return {audio::VoiceActivityState::kSpeech, 1.0F, changed};
}

void MockVoiceActivityDetector::Reset() {
  active_ = false;
}

}  // namespace agent
}  // namespace cockpit
