#pragma once

#include <chrono>
#include <string>

#include "cockpit/modules/voice/assistant/voice_assistant.h"

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

  virtual ActionExecutionResult Execute(VoiceAction action,
                                        std::chrono::steady_clock::time_point deadline) = 0;
  virtual void Cancel() {
  }
};

const char* ToString(ActionExecutionStatus status);

}  // namespace voice
}  // namespace cockpit
