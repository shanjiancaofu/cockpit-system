#include "cockpit/services/recording-service/recording_service.h"

#include <unistd.h>

#include <filesystem>
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

  const bool idle_rejected =
      Check(!service.HandleEvent(event, &error), "idle recording service accepted event") &&
      Check(error == "recording session is not active", "idle event error mismatch") &&
      Check(!service.HandleDataFile(data_file, &error),
            "idle recording service accepted data file") &&
      Check(error == "recording session is not active", "idle data file error mismatch");
  if (!idle_rejected || !Check(service.Start("unit_test", &error), "recording start failed") ||
      !Check(service.HandleEvent(event, &error), "active recording event failed") ||
      !Check(service.HandleDataFile(data_file, &error), "active recording data file failed") ||
      !Check(service.Stop(&error), "recording stop failed")) {
    std::cerr << error << '\n';
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto sessions = service.List(0);
  cockpit::recording::RecordingTimelineQuery query;
  query.limit = 10;
  cockpit::recording::RecordingTimelineResult timeline;
  cockpit::recording::RecordingIntegrityResult integrity;
  const bool result =
      Check(sessions.size() == 1, "completed recording was not cataloged") &&
      Check(service.GetTimeline(sessions.front().session_id, query, &timeline, &error),
            "recording timeline query failed") &&
      Check(timeline.total_entries == 2, "recording timeline total mismatch") &&
      Check(timeline.entries.size() == 2, "recording timeline entry count mismatch") &&
      Check(timeline.entries[0].kind == cockpit::recording::RecordingTimelineEntryKind::kEvent,
            "recording event timeline entry mismatch") &&
      Check(timeline.entries[1].kind == cockpit::recording::RecordingTimelineEntryKind::kDataFile,
            "recording data file timeline entry mismatch") &&
      Check(service.Verify(sessions.front().session_id, &integrity, &error),
            "recording integrity verification failed") &&
      Check(!integrity.healthy, "missing external recording file was reported healthy") &&
      Check(integrity.issues.size() == 1 &&
                integrity.issues[0].kind ==
                    cockpit::recording::RecordingIntegrityIssueKind::kMissingFile,
            "recording integrity missing file issue mismatch");
  std::filesystem::remove_all(root);
  return result ? 0 : 1;
}
