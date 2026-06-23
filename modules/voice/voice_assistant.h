#pragma once

#include <string>

#include "modules/voice/speech_transcript.h"

namespace cockpit {
namespace voice {

enum class VoiceIntent {
  kUnknown,
  kShowVehicleStatus,
  kOpenCamera,
  kPlayMusic,
};

enum class VoiceAction {
  kNone,
  kQueryVehicleStatus,
  kOpenCamera,
  kPlayMusic,
};

struct VoiceAssistantResult {
  VoiceIntent intent = VoiceIntent::kUnknown;
  VoiceAction action = VoiceAction::kNone;
  std::string response_text;
};

class VoiceAssistant {
 public:
  virtual ~VoiceAssistant() = default;

  virtual VoiceAssistantResult HandleTranscript(const SpeechTranscript& transcript) = 0;
};

const char* ToString(VoiceIntent intent);
const char* ToString(VoiceAction action);

}  // namespace voice
}  // namespace cockpit
