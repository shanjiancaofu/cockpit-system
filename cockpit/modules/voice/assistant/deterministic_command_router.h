#pragma once

#include <string_view>

#include "cockpit/modules/voice/assistant/voice_assistant.h"

namespace cockpit {
namespace voice {

struct DeterministicCommandRoute {
  VoiceIntent intent{VoiceIntent::kUnknown};
  VoiceAction action{VoiceAction::kNone};
};

class DeterministicCommandRouter {
 public:
  DeterministicCommandRoute Route(std::string_view normalized_text) const;
};

}  // namespace voice
}  // namespace cockpit
