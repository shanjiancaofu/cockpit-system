#include "cockpit/spider/recording/runtime/recording_service.h"

#include <algorithm>
#include <utility>
#include <sstream>
#include "cockpit/spider/recording/file_checksum.h"
#include "cockpit/exchange/foundation/json/json.h"

#include "cockpit/exchange/foundation/logging/logger.h"

namespace cockpit {
namespace recording {
namespace {

RecordingSessionLimits NormalizeLimits(const std::filesystem::path& root_directory,
                                       const RecordingRetentionPolicy& retention_policy,
                                       RecordingSessionLimits limits) {
  limits.max_total_bytes = retention_policy.max_total_bytes;
  limits.max_session_bytes = std::min(limits.max_session_bytes, limits.max_total_bytes);
  if (limits.allowed_data_root.empty()) {
    limits.allowed_data_root = root_directory.parent_path();
  }
  return limits;
}

}  // namespace

RecordingService::RecordingService(std::filesystem::path root_directory, std::string vehicle_id,
                                   RecordingRetentionPolicy retention_policy,
                                   RecordingMetadata metadata,
                                   RecordingSessionLimits session_limits,
                                   EventVideoRecorderConfig video_config)
    : video_config_(std::move(video_config)), video_(video_config_), session_(root_directory, std::move(vehicle_id), std::move(metadata),
               NormalizeLimits(root_directory, retention_policy, std::move(session_limits))),
      catalog_(std::move(root_directory)),
      retention_policy_(retention_policy) {
}

bool RecordingService::Initialize(std::string* error) {
  return RefreshAndPrune(error);
}

bool RecordingService::Start(const std::string& trigger, std::string* error) {
  std::lock_guard<std::mutex> lock(video_mutex_);
  if (!RefreshAndPrune(error)) {
    return false;
  }
  session_.SetExistingBytes(catalog_.total_bytes());
  return session_.Start(trigger, error);
}

bool RecordingService::Stop(std::string* error) {
  std::lock_guard<std::mutex> lock(video_mutex_);
  if (!FinishVideoLocked("session_stop", error)) return false;
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

bool RecordingService::GetReport(const std::string& session_id, const RecordingReportQuery& query,
                                 RecordingReport* report, std::string* error) const {
  if (report == nullptr) {
    if (error != nullptr) {
      *error = "recording report must not be null";
    }
    return false;
  }
  if (query.timeline_limit == 0 || query.timeline_limit > RecordingTimelineReader::kMaximumLimit ||
      query.issue_limit == 0 || query.issue_limit > kMaximumReportIssueLimit) {
    if (error != nullptr) {
      *error = "recording report limit is invalid";
    }
    return false;
  }

  RecordingReport local_report;
  if (!GetDetail(session_id, &local_report.detail, error)) {
    return false;
  }
  RecordingTimelineQuery timeline_query;
  timeline_query.limit = query.timeline_limit;
  if (!RecordingTimelineReader::Read(local_report.detail.info.directory, timeline_query,
                                     &local_report.timeline, error) ||
      !RecordingIntegrityVerifier::Verify(local_report.detail.info.directory,
                                          &local_report.integrity, error)) {
    return false;
  }
  local_report.total_integrity_issues = local_report.integrity.issues.size();
  local_report.healthy =
      local_report.integrity.healthy && local_report.timeline.corrupted_lines == 0;
  if (local_report.integrity.issues.size() > query.issue_limit) {
    local_report.integrity.issues.resize(query.issue_limit);
    local_report.integrity_issues_truncated = true;
  }
  *report = std::move(local_report);
  return true;
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
  std::lock_guard<std::mutex> lock(video_mutex_);
  RecordingStatus status = session_.status();
  if (!video_error_.empty()) status.last_error = video_error_;
  status.stored_sessions = catalog_.List().size();
  status.stored_bytes = catalog_.total_bytes();
  return status;
}

bool RecordingService::TriggerEventVideo(const std::string& event_id, std::int64_t timestamp_ms,
                                          const std::string& event_type, std::string* path,
                                          std::string* error, std::string* session_id) {
  std::lock_guard<std::mutex> lock(video_mutex_);
  const auto fail = [&](const std::string& message) {
    if (error) *error = message;
    return false;
  };
  if (event_id.empty() || event_id.size() > 128 || event_type.size() > 128 || timestamp_ms <= 0)
    return fail("invalid event video request");
  const auto current = session_.status();
  if (current.state != RecordingState::kRecording) return fail("recording session is not active");
  const auto now = std::chrono::steady_clock::now();
  if (latest_video_pts_ < 0 || now - last_video_at_ > std::chrono::seconds(2) ||
      video_.HistoryStartPts() < 0) return fail("no recent decodable H264 video; wait for a keyframe");
  if (!FinishVideoLocked("retrigger", error)) return false;
  PendingVideo pending;
  pending.event_id = event_id;
  pending.event_type = event_type;
  pending.timestamp_ms = timestamp_ms;
  pending.first_pts = video_.HistoryStartPts();
  pending.trigger_pts = pending.last_pts = latest_video_pts_;
  // Filenames are independent of user supplied event IDs, including repeated IDs.
  const auto name = "event_video_" + std::to_string(++video_sequence_);
  std::string trigger_error;
  if (!video_.Trigger(name, latest_video_pts_, &pending.path, &trigger_error,
                      std::filesystem::path(current.directory) / "artifacts")) {
    video_error_ = trigger_error;
    if (error) *error = trigger_error;
    return false;
  }
  pending_video_ = std::move(pending);
  video_deadline_ = now + std::chrono::milliseconds(video_config_.post_event_ms);
  const RecordingEvent event{timestamp_ms, "/recording/video/trigger",
      "{\"event_id\":\"" + json::EscapeString(event_id) + "\",\"event_type\":\"" +
      json::EscapeString(event_type) + "\",\"video_pts\":" + std::to_string(latest_video_pts_) + "}"};
  if (!session_.AppendEvent(event, error)) {
    video_.Finish();
    std::filesystem::remove(pending_video_.path);
    pending_video_ = {};
    return false;
  }
  video_error_.clear();
  if (session_id) *session_id = current.session_id;
  if (path) *path = std::filesystem::path(pending_video_.path).lexically_relative(current.directory).string();
  return true;
}

bool RecordingService::HandleVideoPacket(const EncodedVideoPacket& packet, bool discontinuity,
                                          std::string* error) {
  std::lock_guard<std::mutex> lock(video_mutex_);
  if (packet.pts < 0 || packet.dts != packet.pts || packet.data.empty()) {
    if (error) *error = "event video requires nonnegative PTS and DTS == PTS";
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (discontinuity || (latest_video_pts_ >= 0 && packet.pts <= latest_video_pts_) ||
      (latest_video_pts_ >= 0 && now - last_video_at_ > std::chrono::seconds(2))) {
    if (!FinishVideoLocked("input_discontinuity", error)) return false;
    video_.ClearHistory();
    latest_video_pts_ = -1;
  }
  latest_video_pts_ = packet.pts;
  last_video_at_ = now;
  if (!pending_video_.path.empty()) pending_video_.last_pts = packet.pts;
  const auto dropped_gops = video_.HistoryDroppedGops();
  std::string video_failure;
  if (!video_.Append(packet, &video_failure)) {
    video_error_ = video_failure;
    // Retire the pending failed artifact now, so a later Poll/Stop cannot
    // reclassify it as a successful file after the muxer was already closed.
    if (!pending_video_.path.empty()) FinishVideoLocked("write_error", nullptr);
    if (error) *error = video_error_;
    return false;
  }
  if (video_.HistoryDroppedGops() != dropped_gops)
    video_error_ = "H264 pre-event GOP exceeded byte/duration limit; history reset, waiting for IDR";
  if (!pending_video_.path.empty() && !video_.active()) return FinishVideoLocked("post_event", error);
  return true;
}

void RecordingService::PollVideo() {
  std::lock_guard<std::mutex> lock(video_mutex_);
  if (pending_video_.path.empty()) return;
  const auto now = std::chrono::steady_clock::now();
  const bool stale = now - last_video_at_ > std::chrono::seconds(2);
  if (stale || now >= video_deadline_ || session_.status().state != RecordingState::kRecording) {
    std::string error;
    if (!FinishVideoLocked(stale ? "input_timeout" : "deadline", &error)) video_error_ = error;
  }
}

bool RecordingService::FinishVideoLocked(const std::string& reason, std::string* error) {
  if (pending_video_.path.empty()) return true;
  std::string failure;
  if (!video_.Finish(&failure)) {
    video_error_ = failure;
    const RecordingEvent failed{pending_video_.timestamp_ms, "/recording/video/failed",
        "{\"event_id\":\"" + json::EscapeString(pending_video_.event_id) +
        "\",\"error\":\"" + json::EscapeString(failure) + "\"}"};
    // Best effort diagnostic event; never index the .incomplete as video/mp4.
    std::string event_error;
    if (!session_.AppendEvent(failed, &event_error)) video_error_ += "; " + event_error;
    pending_video_ = {};
    if (error) *error = video_error_;
    return false;
  }
  const auto current = session_.status();
  RecordingDataFile file;
  file.timestamp_ms = pending_video_.timestamp_ms;
  file.source = "camera_event_video";
  file.kind = "video/mp4";
  file.path = std::filesystem::path(pending_video_.path).lexically_relative(current.directory).string();
  std::error_code filesystem_error;
  file.size_bytes = std::filesystem::file_size(pending_video_.path, filesystem_error);
  if (filesystem_error || !ComputeFnv1a64(pending_video_.path, &file.checksum, error)) {
    if (filesystem_error && error) *error = filesystem_error.message();
    return false;
  }
  // This file is already inside artifacts; its relative path survives session rename.
  if (!session_.AppendGeneratedDataFile(file, error)) return false;
  std::ostringstream payload;
  payload << "{\"event_id\":\"" << json::EscapeString(pending_video_.event_id)
          << "\",\"event_type\":\"" << json::EscapeString(pending_video_.event_type)
          << "\",\"path\":\"" << json::EscapeString(file.path)
          << "\",\"codec\":\"h264\",\"fps\":" << video_config_.fps
          << ",\"time_base\":\"1/" << video_config_.fps
          << "\",\"event_timestamp_ms\":" << pending_video_.timestamp_ms
          << ",\"first_pts\":" << pending_video_.first_pts
          << ",\"trigger_pts\":" << pending_video_.trigger_pts
          << ",\"last_pts\":" << pending_video_.last_pts
          << ",\"finish_reason\":\"" << reason << "\"}";
  const RecordingEvent event{pending_video_.timestamp_ms, "/recording/video/complete", payload.str()};
  pending_video_ = {};
  return session_.AppendEvent(event, error);
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
