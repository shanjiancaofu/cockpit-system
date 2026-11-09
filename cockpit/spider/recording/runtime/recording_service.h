#pragma once

#include <cstdint>
#include <chrono>
#include <mutex>
#include "cockpit/spider/recording/video/event_video_recorder.h"
#include <filesystem>
#include <string>
#include <vector>

#include "cockpit/spider/recording/recording_catalog.h"
#include "cockpit/spider/recording/recording_integrity.h"
#include "cockpit/spider/recording/recording_session.h"
#include "cockpit/spider/recording/recording_timeline.h"
#include "vehicle_state.pb.h"

namespace cockpit {
namespace recording {

struct RecordingReportQuery {
  std::size_t timeline_limit = RecordingTimelineReader::kDefaultLimit;
  std::size_t issue_limit = 100;
};

struct RecordingReport {
  RecordingSessionDetail detail;
  RecordingTimelineResult timeline;
  RecordingIntegrityResult integrity;
  std::uint64_t total_integrity_issues = 0;
  bool integrity_issues_truncated = false;
  bool healthy = false;
};

class RecordingService {
 public:
  static constexpr std::size_t kMaximumReportIssueLimit = 1000;

  RecordingService(std::filesystem::path root_directory, std::string vehicle_id,
                   RecordingRetentionPolicy retention_policy, RecordingMetadata metadata = {},
                   RecordingSessionLimits session_limits = {},
                   EventVideoRecorderConfig video_config = {});

  bool Initialize(std::string* error);
  bool Start(const std::string& trigger, std::string* error);
  bool Stop(std::string* error);
  std::vector<RecordingSessionInfo> List(std::size_t limit) const;
  bool GetDetail(const std::string& session_id, RecordingSessionDetail* detail,
                 std::string* error) const;
  bool GetTimeline(const std::string& session_id, const RecordingTimelineQuery& query,
                   RecordingTimelineResult* result, std::string* error) const;
  bool Verify(const std::string& session_id, RecordingIntegrityResult* result,
              std::string* error) const;
  bool GetReport(const std::string& session_id, const RecordingReportQuery& query,
                 RecordingReport* report, std::string* error) const;
  bool VerifyAll(const RecordingIntegrityBatchQuery& query, RecordingIntegrityBatchResult* result,
                 std::string* error) const;
  bool Delete(const std::string& session_id, std::string* error);
  bool Prune(RecordingPruneResult* result, std::string* error);
  bool HandleEvent(const RecordingEvent& event, std::string* error);
  bool HandleDataFile(const RecordingDataFile& file, std::string* error);
  void HandleVehicleState(const proto::vehicle::VehicleState& state);
  RecordingStatus status() const;
  bool TriggerEventVideo(const std::string& event_id, std::int64_t timestamp_ms,
                         const std::string& event_type, std::string* path, std::string* error,
                         std::string* session_id = nullptr);
  bool HandleVideoPacket(const EncodedVideoPacket& packet, bool discontinuity, std::string* error);
  void PollVideo();

 private:
  bool RefreshAndPrune(std::string* error);

  bool FinishVideoLocked(const std::string& reason, std::string* error);
  mutable std::mutex video_mutex_;
  const EventVideoRecorderConfig video_config_;
  EventVideoRecorder video_;
  struct PendingVideo {
    std::string event_id, event_type, path;
    std::int64_t timestamp_ms = 0, first_pts = 0, trigger_pts = 0, last_pts = 0;
  } pending_video_;
  std::uint64_t video_sequence_ = 0;
  std::int64_t latest_video_pts_ = -1;
  std::chrono::steady_clock::time_point last_video_at_{}, video_deadline_{};
  std::string video_error_;
  RecordingSession session_;
  RecordingCatalog catalog_;
  const RecordingRetentionPolicy retention_policy_;
};

}  // namespace recording
}  // namespace cockpit
