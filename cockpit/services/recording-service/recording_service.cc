#include "cockpit/services/recording-service/recording_service.h"

#include <utility>

#include "cockpit/core/logging/Logger.h"

namespace cockpit {
namespace recording {

RecordingService::RecordingService(std::filesystem::path root_directory, std::string vehicle_id,
                                   RecordingRetentionPolicy retention_policy,
                                   RecordingMetadata metadata)
    : session_(root_directory, std::move(vehicle_id), std::move(metadata)),
      catalog_(std::move(root_directory)),
      retention_policy_(retention_policy) {
}

bool RecordingService::Initialize(std::string* error) {
  return RefreshAndPrune(error);
}

bool RecordingService::Start(const std::string& trigger, std::string* error) {
  if (!RefreshAndPrune(error)) {
    return false;
  }
  return session_.Start(trigger, error);
}

bool RecordingService::Stop(std::string* error) {
  if (!session_.Stop(error)) {
    return false;
  }
  return RefreshAndPrune(error);
}

std::vector<RecordingSessionInfo> RecordingService::List(std::size_t limit) const {
  return catalog_.List(limit);
}

bool RecordingService::Delete(const std::string& session_id, std::string* error) {
  const RecordingStatus current = session_.status();
  if (current.state == RecordingState::kRecording && current.session_id == session_id) {
    if (error != nullptr) {
      *error = "cannot delete the active recording session";
    }
    return false;
  }
  return catalog_.Delete(session_id, error);
}

bool RecordingService::Prune(RecordingPruneResult* result, std::string* error) {
  return catalog_.Prune(retention_policy_, result, error);
}

bool RecordingService::HandleEvent(const RecordingEvent& event, std::string* error) {
  if (session_.status().state != RecordingState::kRecording) {
    return true;
  }
  if (!session_.AppendEvent(event, error)) {
    LOG_ERROR("record generic event failed: " + (error == nullptr ? std::string() : *error));
    return false;
  }
  return true;
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
  RecordingStatus status = session_.status();
  status.stored_sessions = catalog_.List().size();
  status.stored_bytes = catalog_.total_bytes();
  return status;
}

bool RecordingService::RefreshAndPrune(std::string* error) {
  if (!catalog_.Refresh(error)) {
    return false;
  }
  RecordingPruneResult result;
  if (!catalog_.Prune(retention_policy_, &result, error)) {
    return false;
  }
  if (result.sessions_deleted > 0) {
    LOG_INFO("recording retention deleted sessions=" + std::to_string(result.sessions_deleted) +
             " bytes=" + std::to_string(result.bytes_deleted));
  }
  return true;
}

}  // namespace recording
}  // namespace cockpit
