#pragma once

#include "modules/voice/voice_assistant.h"

#include <string>

namespace cockpit {
namespace voice {

enum class ActionExecutionStatus {
  kNotRequested,
  kSucceeded,
  kRejected,
  kNotImplemented,
  kFailed,
};

struct ActionExecutionResult {
  ActionExecutionStatus status = ActionExecutionStatus::kNotRequested;
  std::string message;
};

class ActionDispatcher {
 public:
  virtual ~ActionDispatcher() = default;

  virtual ActionExecutionResult Execute(VoiceAction action) = 0;
};

const char* ToString(ActionExecutionStatus status);

}  // namespace voice
}  // namespace cockpit
