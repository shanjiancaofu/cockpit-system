#include "cockpit/library/recording/recording_service.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("cockpit_recording_service_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  cockpit::recording::RecordingService service(root, "test_vehicle",
                                               {10, std::uint64_t{1024} * 1024U});
  std::string error;
  if (!Check(service.Initialize(&error), "recording service initialization failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  cockpit::recording::RecordingEvent event;
  event.timestamp_ms = 1000;
  event.topic = "/test/event";
  event.payload_json = "{}";
  cockpit::recording::RecordingDataFile data_file;
  data_file.timestamp_ms = 1001;
  data_file.source = "test";
  data_file.kind = "artifact";
  data_file.path = "external.bin";
  const auto checksum_source = root / "checksum.bin";
  std::ofstream(checksum_source, std::ios::binary) << "hello";
  cockpit::recording::RecordingDataFile checksum_file;
  checksum_file.timestamp_ms = 1002;
  checksum_file.source = "test";
  checksum_file.kind = "artifact";
  checksum_file.path = checksum_source.string();
  checksum_file.checksum = "fnv1a64:0000000000000000";
  checksum_file.copy_into_session = true;

  const bool idle_rejected =
      Check(!service.HandleEvent(event, &error), "idle recording service accepted event") &&
      Check(error == "recording session is not active", "idle event error mismatch") &&
      Check(!service.HandleDataFile(data_file, &error),
            "idle recording service accepted data file") &&
      Check(error == "recording session is not active", "idle data file error mismatch");
  if (!idle_rejected || !Check(service.Start("unit_test", &error), "recording start failed") ||
      !Check(service.HandleEvent(event, &error), "active recording event failed") ||
      !Check(service.HandleDataFile(data_file, &error), "active recording data file failed") ||
      !Check(service.HandleDataFile(checksum_file, &error), "active checksum data file failed") ||
      !Check(service.Stop(&error), "recording stop failed")) {
    std::cerr << error << '\n';
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto damaged_sessions = service.List(0);
  std::ofstream(damaged_sessions.front().directory + "/events.jsonl", std::ios::app) << "{bad}\n";
  if (!Check(damaged_sessions.size() == 1, "damaged recording was not cataloged") ||
      !Check(service.Start("healthy", &error), "healthy recording start failed") ||
      !Check(service.Stop(&error), "healthy recording stop failed")) {
    std::cerr << error << '\n';
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto corrupted_directory = root / "sessions" / "corrupted_manual";
  std::filesystem::create_directories(corrupted_directory);
  { std::ofstream complete_marker(corrupted_directory / "COMPLETE"); }
  std::ofstream(corrupted_directory / "manifest.json") << "{invalid";
  if (!Check(service.Initialize(&error), "recording catalog refresh failed")) {
    std::cerr << error << '\n';
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto sessions = service.List(0);
  cockpit::recording::RecordingTimelineQuery query;
  query.limit = 10;
  cockpit::recording::RecordingTimelineResult timeline;
  cockpit::recording::RecordingIntegrityResult integrity;
  cockpit::recording::RecordingIntegrityBatchQuery batch_query;
  batch_query.limit = 10;
  cockpit::recording::RecordingIntegrityBatchResult batch;
  cockpit::recording::RecordingIntegrityBatchQuery limited_query;
  limited_query.limit = 1;
  cockpit::recording::RecordingIntegrityBatchResult limited_batch;
  cockpit::recording::RecordingIntegrityBatchQuery ranged_query;
  ranged_query.from_started_at_ms = 1;
  ranged_query.limit = 10;
  cockpit::recording::RecordingIntegrityBatchResult ranged_batch;
  cockpit::recording::RecordingIntegrityBatchQuery invalid_query;
  invalid_query.from_started_at_ms = 2;
  invalid_query.to_started_at_ms = 1;
  cockpit::recording::RecordingReportQuery report_query;
  report_query.timeline_limit = 10;
  report_query.issue_limit = 10;
  cockpit::recording::RecordingReport damaged_report;
  cockpit::recording::RecordingReportQuery limited_report_query = report_query;
  limited_report_query.issue_limit = 1;
  cockpit::recording::RecordingReport limited_report;
  std::string healthy_session_id;
  std::string healthy_session_directory;
  for (const auto& session : sessions) {
    if (session.trigger == "healthy") {
      healthy_session_id = session.session_id;
      healthy_session_directory = session.directory;
    }
  }
  cockpit::recording::RecordingReport healthy_report;
  cockpit::recording::RecordingReportQuery invalid_report_query;
  invalid_report_query.issue_limit = 0;
  const bool result =
      Check(sessions.size() == 3, "recording catalog size mismatch") &&
      Check(service.GetTimeline(damaged_sessions.front().session_id, query, &timeline, &error),
            "recording timeline query failed") &&
      Check(timeline.total_entries == 3, "recording timeline total mismatch") &&
      Check(timeline.entries.size() == 3, "recording timeline entry count mismatch") &&
      Check(timeline.corrupted_lines == 1, "recording timeline corruption count mismatch") &&
      Check(timeline.entries[0].kind == cockpit::recording::RecordingTimelineEntryKind::kEvent,
            "recording event timeline entry mismatch") &&
      Check(timeline.entries[1].kind == cockpit::recording::RecordingTimelineEntryKind::kDataFile,
            "recording data file timeline entry mismatch") &&
      Check(service.Verify(damaged_sessions.front().session_id, &integrity, &error),
            "recording integrity verification failed") &&
      Check(!integrity.healthy, "missing external recording file was reported healthy") &&
      Check(integrity.issues.size() == 2 &&
                integrity.issues[0].kind ==
                    cockpit::recording::RecordingIntegrityIssueKind::kMissingFile &&
                integrity.issues[1].kind ==
                    cockpit::recording::RecordingIntegrityIssueKind::kChecksumMismatch,
            "recording integrity issue mismatch") &&
      Check(service.GetReport(damaged_sessions.front().session_id, report_query, &damaged_report,
                              &error),
            "damaged recording report failed") &&
      Check(damaged_report.detail.data_files_indexed == 2 &&
                damaged_report.timeline.total_entries == 3 &&
                damaged_report.timeline.corrupted_lines == 1 && !damaged_report.integrity.healthy &&
                damaged_report.total_integrity_issues == 2 &&
                damaged_report.integrity.issues.size() == 2 &&
                !damaged_report.integrity_issues_truncated && !damaged_report.healthy,
            "damaged recording report mismatch") &&
      Check(service.GetReport(damaged_sessions.front().session_id, limited_report_query,
                              &limited_report, &error),
            "limited recording report failed") &&
      Check(limited_report.total_integrity_issues == 2 &&
                limited_report.integrity.issues.size() == 1 &&
                limited_report.integrity_issues_truncated,
            "recording report issue limit mismatch") &&
      Check(!healthy_session_id.empty(), "healthy recording session was not found") &&
      Check(service.GetReport(healthy_session_id, report_query, &healthy_report, &error),
            "healthy recording report failed") &&
      Check(healthy_report.integrity.healthy && healthy_report.total_integrity_issues == 0 &&
                healthy_report.timeline.corrupted_lines == 0 && healthy_report.healthy,
            "healthy recording report mismatch") &&
      Check(!service.GetReport(damaged_sessions.front().session_id, invalid_report_query,
                               &damaged_report, &error),
            "invalid recording report limit was accepted") &&
      Check(service.VerifyAll(batch_query, &batch, &error),
            "batch recording verification failed") &&
      Check(batch.total_sessions == 3 && batch.sessions.size() == 3,
            "batch recording verification total mismatch") &&
      Check(batch.healthy_sessions == 1 && batch.damaged_sessions == 1 &&
                batch.unavailable_sessions == 1,
            "batch recording verification state counts mismatch") &&
      Check(!batch.truncated, "complete batch was reported truncated") &&
      Check(service.VerifyAll(limited_query, &limited_batch, &error),
            "limited batch recording verification failed") &&
      Check(limited_batch.total_sessions == 3 && limited_batch.sessions.size() == 1 &&
                limited_batch.truncated,
            "limited batch recording verification mismatch") &&
      Check(limited_batch.healthy_sessions + limited_batch.damaged_sessions +
                    limited_batch.unavailable_sessions ==
                1,
            "limited batch state counts mismatch") &&
      Check(service.VerifyAll(ranged_query, &ranged_batch, &error),
            "ranged batch recording verification failed") &&
      Check(ranged_batch.total_sessions == 2 && ranged_batch.unavailable_sessions == 0,
            "ranged batch recording verification mismatch") &&
      Check(!service.VerifyAll(invalid_query, &batch, &error),
            "invalid batch recording range was accepted");

  bool corrupted_timeline_result = false;
  if (result) {
    std::ofstream(healthy_session_directory + "/events.jsonl", std::ios::app) << "{bad}\n";
    cockpit::recording::RecordingReport corrupted_timeline_report;
    corrupted_timeline_result =
        Check(
            service.GetReport(healthy_session_id, report_query, &corrupted_timeline_report, &error),
            "corrupted timeline report failed") &&
        Check(!corrupted_timeline_report.healthy && corrupted_timeline_report.integrity.healthy &&
                  corrupted_timeline_report.timeline.corrupted_lines == 1,
              "corrupted timeline report was healthy");
  }

  cockpit::recording::RecordingService empty_service(root / "empty", "test_vehicle",
                                                     {10, std::uint64_t{1024} * 1024U});
  cockpit::recording::RecordingIntegrityBatchResult empty_batch;
  const bool empty_result =
      Check(empty_service.Initialize(&error), "empty recording service initialization failed") &&
      Check(empty_service.VerifyAll(batch_query, &empty_batch, &error),
            "empty batch recording verification failed") &&
      Check(empty_batch.total_sessions == 0 && empty_batch.sessions.empty() &&
                empty_batch.healthy_sessions == 0 && empty_batch.damaged_sessions == 0 &&
                empty_batch.unavailable_sessions == 0 && !empty_batch.truncated,
            "empty batch recording verification mismatch");
  std::filesystem::remove_all(root);
  return result && corrupted_timeline_result && empty_result ? 0 : 1;
}
