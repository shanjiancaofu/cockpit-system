#pragma once

#include "modules/voice/assistant/voice_assistant.h"

namespace cockpit {
namespace voice {

class MockVoiceAssistant final : public VoiceAssistant {
 public:
  VoiceAssistantResult HandleTranscript(const SpeechTranscript& transcript) override;
};

}  // namespace voice
}  // namespace cockpit
