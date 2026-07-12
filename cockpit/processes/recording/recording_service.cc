#include "cockpit/processes/recording/recording_service.h"

#include <utility>

#include "cockpit/core/logging/logger.h"

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

bool RecordingService::GetDetail(const std::string& session_id, RecordingSessionDetail* detail,
                                 std::string* error) const {
  return catalog_.GetDetail(session_id, detail, error);
}

bool RecordingService::GetTimeline(const std::string& session_id,
                                   const RecordingTimelineQuery& query,
                                   RecordingTimelineResult* result, std::string* error) const {
  RecordingSessionDetail detail;
  if (!catalog_.GetDetail(session_id, &detail, error)) {
    return false;
  }
  return RecordingTimelineReader::Read(detail.info.directory, query, result, error);
}

bool RecordingService::Verify(const std::string& session_id, RecordingIntegrityResult* result,
                              std::string* error) const {
  RecordingSessionDetail detail;
  if (!catalog_.GetDetail(session_id, &detail, error)) {
    return false;
  }
  return RecordingIntegrityVerifier::Verify(detail.info.directory, result, error);
}

bool RecordingService::VerifyAll(const RecordingIntegrityBatchQuery& query,
                                 RecordingIntegrityBatchResult* result, std::string* error) const {
  constexpr std::size_t kMaximumLimit = 1000;
  if (result == nullptr) {
    if (error != nullptr) {
      *error = "recording integrity batch result must not be null";
    }
    return false;
  }
  if (query.from_started_at_ms < 0 || query.to_started_at_ms < 0 || query.limit == 0 ||
      query.limit > kMaximumLimit ||
      (query.to_started_at_ms > 0 && query.from_started_at_ms > query.to_started_at_ms)) {
    if (error != nullptr) {
      *error = "recording integrity batch query is invalid";
    }
    return false;
  }

  std::vector<RecordingSessionInfo> filtered;
  for (const auto& session : catalog_.List(0)) {
    if (session.started_at_ms >= query.from_started_at_ms &&
        (query.to_started_at_ms == 0 || session.started_at_ms <= query.to_started_at_ms)) {
      filtered.push_back(session);
    }
  }
  RecordingIntegrityBatchResult local_result;
  local_result.total_sessions = filtered.size();
  local_result.truncated = filtered.size() > query.limit;
  if (filtered.size() > query.limit) {
    filtered.resize(query.limit);
  }
  for (const auto& session : filtered) {
    RecordingSessionIntegritySummary summary;
    summary.session_id = session.session_id;
    summary.started_at_ms = session.started_at_ms;
    RecordingIntegrityResult integrity;
    std::string verify_error;
    if (!Verify(session.session_id, &integrity, &verify_error)) {
      summary.state = RecordingSessionIntegrityState::kUnavailable;
      summary.error = std::move(verify_error);
      ++local_result.unavailable_sessions;
    } else if (!integrity.healthy) {
      summary.state = RecordingSessionIntegrityState::kDamaged;
      summary.files_checked = integrity.files_checked;
      summary.issues = integrity.issues.size();
      ++local_result.damaged_sessions;
    } else {
      summary.state = RecordingSessionIntegrityState::kHealthy;
      summary.files_checked = integrity.files_checked;
      ++local_result.healthy_sessions;
    }
    local_result.sessions.push_back(std::move(summary));
  }
  *result = std::move(local_result);
  return true;
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
    if (error != nullptr) {
      *error = "recording session is not active";
    }
    return false;
  }
  if (!session_.AppendEvent(event, error)) {
    LOG_ERROR("record generic event failed: " + (error == nullptr ? std::string() : *error));
    return false;
  }
  return true;
}

bool RecordingService::HandleDataFile(const RecordingDataFile& file, std::string* error) {
  if (session_.status().state != RecordingState::kRecording) {
    if (error != nullptr) {
      *error = "recording session is not active";
    }
    return false;
  }
  if (!session_.AppendDataFile(file, error)) {
    LOG_ERROR("record data file index failed: " + (error == nullptr ? std::string() : *error));
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
