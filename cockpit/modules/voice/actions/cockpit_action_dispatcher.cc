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

ActionExecutionResult CockpitActionDispatcher::Execute(VoiceAction action,
                                                       const ActionExecutionContext& context) {
  if (context.IsCancellationRequested()) {
    return {ActionExecutionStatus::kFailed, "Action execution cancelled."};
  }
  if (std::chrono::steady_clock::now() >= context.deadline) {
    return {ActionExecutionStatus::kFailed, "Action execution deadline exceeded."};
  }
  switch (action) {
    case VoiceAction::kNone:
      return {ActionExecutionStatus::kNotRequested, "No action requested."};
    case VoiceAction::kQueryVehicleStatus:
      return QueryVehicleStatus(context);
    case VoiceAction::kOpenCamera:
      return SendHmiCommand(HmiCommand::kOpenCameraPreview,
                            "HMI camera preview provider is not configured.", context);
    case VoiceAction::kPlayMusic:
      return SendHmiCommand(HmiCommand::kPlayMusic, "HMI media provider is not configured.",
                            context);
  }
  return {ActionExecutionStatus::kRejected, "Action is not allowlisted."};
}

void CockpitActionDispatcher::Cancel() {
  ActiveProvider active = ActiveProvider::kNone;
  {
    std::lock_guard<std::mutex> lock(active_mutex_);
    active = active_provider_;
  }
  if (active == ActiveProvider::kVehicle && vehicle_status_ != nullptr) {
    vehicle_status_->Cancel();
  } else if (active == ActiveProvider::kHmi && hmi_commands_ != nullptr) {
    hmi_commands_->Cancel();
  }
}

ActionExecutionResult CockpitActionDispatcher::QueryVehicleStatus(
    const ActionExecutionContext& context) {
  if (vehicle_status_ == nullptr) {
    return {ActionExecutionStatus::kNotImplemented, "Vehicle status provider is not configured."};
  }
  VehicleStatusSnapshot status;
  std::string error;
  SetActiveProvider(ActiveProvider::kVehicle);
  if (context.IsCancellationRequested()) {
    ClearActiveProvider(ActiveProvider::kVehicle);
    return {ActionExecutionStatus::kFailed, "Action execution cancelled."};
  }
  const bool succeeded = vehicle_status_->GetLatest(context, &status, &error);
  ClearActiveProvider(ActiveProvider::kVehicle);
  if (!succeeded) {
    return {ActionExecutionStatus::kFailed,
            error.empty() ? "Vehicle status is unavailable." : error};
  }
  std::ostringstream message;
  message << std::fixed << std::setprecision(1) << "Vehicle speed is " << status.speed_kph
          << " kilometers per hour, battery is " << status.soc_percent
          << " percent, and gear code is " << status.gear << '.';
  return {ActionExecutionStatus::kSucceeded, message.str()};
}

ActionExecutionResult CockpitActionDispatcher::SendHmiCommand(
    HmiCommand command, const char* not_configured_message, const ActionExecutionContext& context) {
  if (hmi_commands_ == nullptr) {
    return {ActionExecutionStatus::kNotImplemented, not_configured_message};
  }
  std::string response;
  std::string error;
  SetActiveProvider(ActiveProvider::kHmi);
  if (context.IsCancellationRequested()) {
    ClearActiveProvider(ActiveProvider::kHmi);
    return {ActionExecutionStatus::kFailed, "Action execution cancelled."};
  }
  const bool succeeded = hmi_commands_->SendCommand(command, context, &response, &error);
  ClearActiveProvider(ActiveProvider::kHmi);
  if (!succeeded) {
    return {ActionExecutionStatus::kFailed, error.empty() ? "HMI command failed." : error};
  }
  if (response.empty()) {
    response = std::string("HMI command accepted: ") + ToString(command);
  }
  return {ActionExecutionStatus::kSucceeded, response};
}

void CockpitActionDispatcher::SetActiveProvider(ActiveProvider provider) {
  std::lock_guard<std::mutex> lock(active_mutex_);
  active_provider_ = provider;
}

void CockpitActionDispatcher::ClearActiveProvider(ActiveProvider provider) {
  std::lock_guard<std::mutex> lock(active_mutex_);
  if (active_provider_ == provider) {
    active_provider_ = ActiveProvider::kNone;
  }
}

}  // namespace voice
}  // namespace cockpit
