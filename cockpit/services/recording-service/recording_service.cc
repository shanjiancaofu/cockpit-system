#include "cockpit/services/recording-service/recording_service.h"

#include <utility>

#include "cockpit/core/logging/Logger.h"

namespace cockpit {
namespace recording {

RecordingService::RecordingService(std::filesystem::path root_directory, std::string vehicle_id)
    : session_(std::move(root_directory), std::move(vehicle_id)) {
}

bool RecordingService::Start(const std::string& trigger, std::string* error) {
  return session_.Start(trigger, error);
}

bool RecordingService::Stop(std::string* error) {
  return session_.Stop(error);
}

void RecordingService::HandleVehicleState(const proto::vehicle::VehicleState& state) {
  if (session_.status().state != RecordingState::kRecording) {
    return;
  }
  vehicle::VehicleState recorded_state;
  recorded_state.timestamp_ms = state.timestamp_ms();
  recorded_state.speed_kph = state.speed_kph();
  recorded_state.gear = state.gear();
  recorded_state.soc_percent = state.soc_percent();
  recorded_state.cloud_enabled = state.cloud_enabled();
  recorded_state.source = state.source();
  std::string error;
  if (!session_.Append(recorded_state, &error)) {
    LOG_ERROR("record vehicle state failed: " + error);
  }
}

RecordingStatus RecordingService::status() const {
  return session_.status();
}

}  // namespace recording
}  // namespace cockpit
