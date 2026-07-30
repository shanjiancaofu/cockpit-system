#pragma once

#include "agent/speech/vad/voice_activity_detector.h"

namespace cockpit {
namespace agent {

class MockVoiceActivityDetector final : public audio::VoiceActivityDetector {
 public:
  audio::VoiceActivityResult Analyze(const audio::AudioFrame& frame) override;
  void Reset() override;

 private:
  bool active_{false};
};

}  // namespace agent
}  // namespace cockpit
