#pragma once

#include "cockpit/modules/voice/assistant/voice_assistant.h"

namespace cockpit {
namespace voice {

class MockVoiceAssistant final : public VoiceAssistant {
 public:
  VoiceAssistantResult HandleTranscript(const SpeechTranscript& transcript,
                                        std::chrono::steady_clock::time_point deadline) override;
  void Cancel() override {
  }
};

}  // namespace voice
}  // namespace cockpit
