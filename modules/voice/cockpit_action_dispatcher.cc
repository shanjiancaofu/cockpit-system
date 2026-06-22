#include "modules/voice/cockpit_action_dispatcher.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace cockpit {
namespace voice {

CockpitActionDispatcher::CockpitActionDispatcher(
    std::unique_ptr<VehicleStatusProvider> vehicle_status)
    : vehicle_status_(std::move(vehicle_status)) {}

ActionExecutionResult CockpitActionDispatcher::Execute(VoiceAction action) {
  switch (action) {
    case VoiceAction::kNone:
      return {ActionExecutionStatus::kNotRequested, "No action requested."};
    case VoiceAction::kQueryVehicleStatus:
      return QueryVehicleStatus();
    case VoiceAction::kOpenCamera:
      return {ActionExecutionStatus::kNotImplemented,
              "Camera control is not implemented yet."};
    case VoiceAction::kPlayMusic:
      return {ActionExecutionStatus::kNotImplemented,
              "Music playback control is not implemented yet."};
    case VoiceAction::kStartRecording:
    case VoiceAction::kStopRecording:
      return {ActionExecutionStatus::kNotImplemented,
              "Recorder control is not implemented yet."};
  }
  return {ActionExecutionStatus::kRejected, "Action is not allowlisted."};
}

ActionExecutionResult CockpitActionDispatcher::QueryVehicleStatus() {
  if (vehicle_status_ == nullptr) {
    return {ActionExecutionStatus::kNotImplemented,
            "Vehicle status provider is not configured."};
  }
  VehicleStatusSnapshot status;
  std::string error;
  if (!vehicle_status_->GetLatest(&status, &error)) {
    return {ActionExecutionStatus::kFailed,
            error.empty() ? "Vehicle status is unavailable." : error};
  }
  std::ostringstream message;
  message << std::fixed << std::setprecision(1)
          << "Vehicle speed is " << status.speed_kph
          << " kilometers per hour, battery is " << status.soc_percent
          << " percent, and gear code is " << status.gear << '.';
  return {ActionExecutionStatus::kSucceeded, message.str()};
}

}  // namespace voice
}  // namespace cockpit
