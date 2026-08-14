#pragma once

#include <atomic>
#include <chrono>
#include <memory>
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

class ActionCancellation {
 public:
  void RequestCancellation() {
    requested_.store(true);
  }

  bool IsCancellationRequested() const {
    return requested_.load();
  }

 private:
  std::atomic_bool requested_{false};
};

struct ActionExecutionContext {
  std::chrono::steady_clock::time_point deadline;
  std::shared_ptr<const ActionCancellation> cancellation;

  bool IsCancellationRequested() const {
    return cancellation != nullptr && cancellation->IsCancellationRequested();
  }
};

class ActionDispatcher {
 public:
  virtual ~ActionDispatcher() = default;

  virtual ActionExecutionResult Execute(VoiceAction action,
                                        const ActionExecutionContext& context) = 0;
  virtual void Cancel() {
  }
};

const char* ToString(ActionExecutionStatus status);

}  // namespace voice
}  // namespace cockpit
