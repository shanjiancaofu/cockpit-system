#include "cockpit/modules/recording/recording_timeline.h"

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
  const auto directory = std::filesystem::temp_directory_path() /
                         ("cockpit_recording_timeline_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  std::ofstream(directory / "vehicle_state.jsonl")
      << "{\"timestamp_ms\":1000,\"speed_kph\":10.0,\"source\":\"mock\"}\n";
  std::ofstream(directory / "events.jsonl")
      << "{\"timestamp_ms\":1000,\"topic\":\"/camera/status\",\"payload\":{}}\n"
      << "{bad}\n";
  std::ofstream(directory / "data_files.jsonl")
      << "{\"timestamp_ms\":1020,\"source\":\"camera\",\"kind\":\"jpeg\","
         "\"path\":\"artifacts/1_photo.jpg\"}\n";

  cockpit::recording::RecordingTimelineQuery query;
  query.from_timestamp_ms = 1000;
  query.to_timestamp_ms = 1010;
  query.limit = 10;
  cockpit::recording::RecordingTimelineResult result;
  std::string error;
  const bool range_ok =
      Check(cockpit::recording::RecordingTimelineReader::Read(directory, query, &result, &error),
            "read filtered recording timeline failed") &&
      Check(result.entries.size() == 2, "recording timeline range filter mismatch") &&
      Check(result.total_entries == 2, "recording timeline filtered total mismatch") &&
      Check(result.corrupted_lines == 1, "recording timeline corrupted line count mismatch") &&
      Check(!result.truncated, "filtered recording timeline was unexpectedly truncated") &&
      Check(result.entries[0].kind == cockpit::recording::RecordingTimelineEntryKind::kVehicleState,
            "equal timestamp ordering was not stable") &&
      Check(result.entries[1].label == "/camera/status", "event topic was not extracted");

  query.from_timestamp_ms = 0;
  query.to_timestamp_ms = 0;
  query.limit = 1;
  const bool limit_ok =
      Check(cockpit::recording::RecordingTimelineReader::Read(directory, query, &result, &error),
            "read limited recording timeline failed") &&
      Check(result.entries.size() == 1 && result.total_entries == 3 && result.truncated,
            "recording timeline limit metadata mismatch");

  query.from_timestamp_ms = 2000;
  query.to_timestamp_ms = 1000;
  const bool invalid_rejected =
      Check(!cockpit::recording::RecordingTimelineReader::Read(directory, query, &result, &error),
            "invalid recording timeline range was accepted");
  std::filesystem::remove_all(directory);
  return range_ok && limit_ok && invalid_rejected ? 0 : 1;
}
