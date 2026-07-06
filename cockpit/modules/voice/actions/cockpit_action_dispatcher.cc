#include "cockpit/modules/voice/actions/cockpit_action_dispatcher.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace cockpit {
namespace voice {

CockpitActionDispatcher::CockpitActionDispatcher(
    std::unique_ptr<VehicleStatusProvider> vehicle_status)
    : CockpitActionDispatcher(std::move(vehicle_status), nullptr) {
}

CockpitActionDispatcher::CockpitActionDispatcher(
    std::unique_ptr<VehicleStatusProvider> vehicle_status,
    std::unique_ptr<HmiCommandProvider> hmi_commands)
    : vehicle_status_(std::move(vehicle_status)), hmi_commands_(std::move(hmi_commands)) {
}

ActionExecutionResult CockpitActionDispatcher::Execute(VoiceAction action) {
  switch (action) {
    case VoiceAction::kNone:
      return {ActionExecutionStatus::kNotRequested, "No action requested."};
    case VoiceAction::kQueryVehicleStatus:
      return QueryVehicleStatus();
    case VoiceAction::kOpenCamera:
      return SendHmiCommand(HmiCommand::kOpenCameraPreview,
                            "HMI camera preview provider is not configured.");
    case VoiceAction::kPlayMusic:
      return SendHmiCommand(HmiCommand::kPlayMusic, "HMI media provider is not configured.");
  }
  return {ActionExecutionStatus::kRejected, "Action is not allowlisted."};
}

ActionExecutionResult CockpitActionDispatcher::QueryVehicleStatus() {
  if (vehicle_status_ == nullptr) {
    return {ActionExecutionStatus::kNotImplemented, "Vehicle status provider is not configured."};
  }
  VehicleStatusSnapshot status;
  std::string error;
  if (!vehicle_status_->GetLatest(&status, &error)) {
    return {ActionExecutionStatus::kFailed,
            error.empty() ? "Vehicle status is unavailable." : error};
  }
  std::ostringstream message;
  message << std::fixed << std::setprecision(1) << "Vehicle speed is " << status.speed_kph
          << " kilometers per hour, battery is " << status.soc_percent
          << " percent, and gear code is " << status.gear << '.';
  return {ActionExecutionStatus::kSucceeded, message.str()};
}

ActionExecutionResult CockpitActionDispatcher::SendHmiCommand(HmiCommand command,
                                                              const char* not_configured_message) {
  if (hmi_commands_ == nullptr) {
    return {ActionExecutionStatus::kNotImplemented, not_configured_message};
  }
  std::string response;
  std::string error;
  if (!hmi_commands_->SendCommand(command, &response, &error)) {
    return {ActionExecutionStatus::kFailed, error.empty() ? "HMI command failed." : error};
  }
  if (response.empty()) {
    response = std::string("HMI command accepted: ") + ToString(command);
  }
  return {ActionExecutionStatus::kSucceeded, response};
}

}  // namespace voice
}  // namespace cockpit
