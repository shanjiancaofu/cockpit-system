#include "cockpit/modules/recording/recording_session.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("cockpit_recording_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(root);

  cockpit::recording::RecordingMetadata metadata;
  metadata.config_path = "configs/test.yaml";
  metadata.config_checksum = "fnv1a64:test";
  metadata.git_commit = "abc123";
  metadata.git_dirty = true;
  metadata.build_type = "Debug";
  metadata.binary_version = "0.1.0";
  metadata.sources = {"vehicle_state", "camera_frame_meta", "voice_response"};
  cockpit::recording::RecordingSession session(root, "test_vehicle", metadata);
  std::string error;
  if (!Check(session.Start("unit_test", &error), "start recording session failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  cockpit::vehicle::VehicleState first;
  first.timestamp_ms = 1000;
  first.speed_kph = 12.5;
  first.gear = 1;
  first.soc_percent = 80;
  first.source = "test";
  auto second = first;
  second.timestamp_ms = 1020;
  second.speed_kph = 13.0;

  cockpit::recording::RecordingEvent event;
  event.timestamp_ms = 1030;
  event.topic = "/camera/frame_meta";
  event.payload_json = "{\"sequence\":7,\"width\":640,\"height\":480}";
  cockpit::recording::RecordingDataFile data_file;
  data_file.timestamp_ms = 1040;
  data_file.source = "camera";
  data_file.kind = "jpeg";
  data_file.path = "photos/frame_7.jpg";
  data_file.size_bytes = 4096;
  data_file.checksum = "sha256:test";

  if (!Check(session.Append(first, &error), "append first state failed") ||
      !Check(session.Append(second, &error), "append second state failed") ||
      !Check(session.AppendEvent(event, &error), "append camera event failed") ||
      !Check(session.AppendDataFile(data_file, &error), "append camera data file failed") ||
      !Check(session.Stop(&error), "stop recording session failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  const auto status = session.status();
  const std::filesystem::path directory(status.directory);
  const std::string manifest = ReadFile(directory / "manifest.json");
  const std::string states = ReadFile(directory / "vehicle_state.jsonl");
  const std::string events = ReadFile(directory / "events.jsonl");
  const std::string data_files = ReadFile(directory / "data_files.jsonl");
  const auto interrupted_source = root / "sessions" / ".recording_stale";
  std::filesystem::create_directories(interrupted_source);
  std::ofstream(interrupted_source / "vehicle_state.jsonl") << "partial\n";
  const std::size_t recovered =
      cockpit::recording::RecordingSession::RecoverInterrupted(root, &error);
  const auto interrupted_directory = root / "sessions" / "interrupted_stale";
  const bool result =
      Check(status.state == cockpit::recording::RecordingState::kIdle,
            "recording session did not return to idle") &&
      Check(status.messages_written == 4, "recording message count mismatch") &&
      Check(status.data_files_indexed == 1, "recording data file index count mismatch") &&
      Check(std::filesystem::exists(directory / "COMPLETE"), "recording COMPLETE marker missing") &&
      Check(manifest.find("\"state\": \"complete\"") != std::string::npos,
            "recording manifest state mismatch") &&
      Check(manifest.find("\"messages_written\": 4") != std::string::npos,
            "recording manifest message count mismatch") &&
      Check(manifest.find("\"data_files_indexed\": 1") != std::string::npos,
            "recording manifest data file count missing") &&
      Check(manifest.find("\"project\": \"cockpit-system\"") != std::string::npos,
            "recording manifest project metadata missing") &&
      Check(manifest.find("\"config_path\": \"configs/test.yaml\"") != std::string::npos,
            "recording manifest config path missing") &&
      Check(manifest.find("\"config_checksum\": \"fnv1a64:test\"") != std::string::npos,
            "recording manifest config checksum missing") &&
      Check(manifest.find("\"git_commit\": \"abc123\"") != std::string::npos,
            "recording manifest git commit missing") &&
      Check(manifest.find("\"git_dirty\": true") != std::string::npos,
            "recording manifest git dirty missing") &&
      Check(manifest.find("\"build_type\": \"Debug\"") != std::string::npos,
            "recording manifest build type missing") &&
      Check(manifest.find("\"binary_version\": \"0.1.0\"") != std::string::npos,
            "recording manifest binary version missing") &&
      Check(manifest.find("\"camera_frame_meta\"") != std::string::npos,
            "recording manifest source metadata missing") &&
      Check(states.find("\"timestamp_ms\":1000") != std::string::npos,
            "first vehicle state missing") &&
      Check(states.find("\"timestamp_ms\":1020") != std::string::npos,
            "second vehicle state missing") &&
      Check(events.find("\"topic\":\"/camera/frame_meta\"") != std::string::npos,
            "camera event topic missing") &&
      Check(events.find("\"sequence\":7") != std::string::npos, "camera event payload missing") &&
      Check(data_files.find("\"path\":\"photos/frame_7.jpg\"") != std::string::npos,
            "camera data file path missing") &&
      Check(data_files.find("\"checksum\":\"sha256:test\"") != std::string::npos,
            "camera data file checksum missing") &&
      Check(recovered == 1, "interrupted recording recovery count mismatch") &&
      Check(std::filesystem::exists(interrupted_directory / "INTERRUPTED"),
            "interrupted recording marker missing");
  std::filesystem::remove_all(root);
  return result ? 0 : 1;
}
