#include "cockpit/modules/voice/actions/mock_action_dispatcher.h"

#include <string>

namespace cockpit {
namespace voice {

ActionExecutionResult MockActionDispatcher::Execute(VoiceAction action,
                                                    const ActionExecutionContext& context) {
  if (context.IsCancellationRequested()) {
    return {ActionExecutionStatus::kFailed, "Action execution cancelled."};
  }
  if (std::chrono::steady_clock::now() >= context.deadline) {
    return {ActionExecutionStatus::kFailed, "Mock action deadline exceeded."};
  }
  switch (action) {
    case VoiceAction::kNone:
      return {ActionExecutionStatus::kNotRequested, "No action requested."};
    case VoiceAction::kQueryVehicleStatus:
    case VoiceAction::kOpenCamera:
    case VoiceAction::kPlayMusic:
      return {ActionExecutionStatus::kSucceeded,
              std::string("Mock action completed: ") + ToString(action)};
  }
  return {ActionExecutionStatus::kRejected, "Action is not allowlisted."};
}

const char* ToString(ActionExecutionStatus status) {
  switch (status) {
    case ActionExecutionStatus::kNotRequested:
      return "not_requested";
    case ActionExecutionStatus::kSucceeded:
      return "succeeded";
    case ActionExecutionStatus::kRejected:
      return "rejected";
    case ActionExecutionStatus::kNotImplemented:
      return "not_implemented";
    case ActionExecutionStatus::kFailed:
      return "failed";
  }
  return "failed";
}

}  // namespace voice
}  // namespace cockpit
