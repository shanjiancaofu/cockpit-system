#include "modules/voice/mock_action_dispatcher.h"

#include <string>

namespace cockpit {
namespace voice {

ActionExecutionResult MockActionDispatcher::Execute(VoiceAction action) {
  switch (action) {
    case VoiceAction::kNone:
      return {ActionExecutionStatus::kNotRequested, "No action requested."};
    case VoiceAction::kQueryVehicleStatus:
    case VoiceAction::kOpenCamera:
    case VoiceAction::kPlayMusic:
    case VoiceAction::kStartRecording:
    case VoiceAction::kStopRecording:
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
