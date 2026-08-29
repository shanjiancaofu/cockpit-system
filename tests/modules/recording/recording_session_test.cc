#include "cockpit/modules/recording/recording_session.h"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

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
  cockpit::recording::RecordingEvent invalid_event = event;
  invalid_event.payload_json = "{bad}";
  cockpit::recording::RecordingDataFile data_file;
  data_file.timestamp_ms = 1040;
  data_file.source = "camera";
  data_file.kind = "jpeg";
  data_file.path = "photos/frame_7.jpg";
  data_file.size_bytes = 4096;
  data_file.checksum = "sha256:test";
  const auto source_artifact = root / "captured_photo.jpg";
  std::ofstream(source_artifact, std::ios::binary) << "jpeg-test-data";
  cockpit::recording::RecordingDataFile copied_data_file;
  copied_data_file.timestamp_ms = 1050;
  copied_data_file.source = "camera";
  copied_data_file.kind = "jpeg";
  copied_data_file.path = source_artifact.string();
  copied_data_file.copy_into_session = true;

  if (!Check(!session.AppendEvent(invalid_event, &error), "invalid JSON event was accepted") ||
      !Check(session.Append(first, &error), "append first state failed") ||
      !Check(session.Append(second, &error), "append second state failed") ||
      !Check(session.AppendEvent(event, &error), "append camera event failed") ||
      !Check(session.AppendDataFile(data_file, &error), "append camera data file failed") ||
      !Check(session.AppendDataFile(copied_data_file, &error),
             "copy camera data file into session failed") ||
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
  const auto copied_artifact = directory / "artifacts" / "2_captured_photo.jpg";
  const auto interrupted_source = root / "sessions" / ".recording_stale";
  std::filesystem::create_directories(interrupted_source);
  std::ofstream(interrupted_source / "vehicle_state.jsonl") << "partial\n";
  const std::size_t recovered =
      cockpit::recording::RecordingSession::RecoverInterrupted(root, &error);
  const auto interrupted_directory = root / "sessions" / "interrupted_stale";
  cockpit::recording::RecordingSessionLimits byte_limits;
  byte_limits.max_session_bytes = 16;
  byte_limits.max_total_bytes = 16;
  byte_limits.min_free_bytes = 1;
  byte_limits.allowed_data_root = root;
  cockpit::recording::RecordingSession limited_session(root / "limited", "test_vehicle", {},
                                                       byte_limits);
  cockpit::recording::RecordingEvent oversized_event = event;
  std::string limit_error;
  const bool byte_limit_result =
      Check(limited_session.Start("byte_limit", &limit_error),
            "limited recording session did not start") &&
      Check(!limited_session.AppendEvent(oversized_event, &limit_error),
            "limited recording session accepted an oversized event") &&
      Check(limited_session.status().state == cockpit::recording::RecordingState::kFaulted &&
                limit_error.find("byte limit") != std::string::npos,
            "limited recording session did not enter a diagnostic fault");

  cockpit::recording::RecordingSessionLimits duration_limits;
  duration_limits.max_duration_ms = 1;
  duration_limits.min_free_bytes = 1;
  duration_limits.allowed_data_root = root;
  cockpit::recording::RecordingSession duration_session(root / "duration", "test_vehicle", {},
                                                        duration_limits);
  std::string duration_error;
  bool duration_limit_result = Check(duration_session.Start("duration_limit", &duration_error),
                                     "duration-limited recording session did not start");
  if (duration_limit_result) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    duration_limit_result = Check(!duration_session.AppendEvent(event, &duration_error),
                                  "recording session exceeded its duration limit") &&
                            Check(duration_error.find("duration limit") != std::string::npos,
                                  "recording duration limit error was not diagnostic");
  }

  const auto allowed_root = root / "allowed";
  std::filesystem::create_directories(allowed_root);
  const auto blocked_source = root / "blocked.bin";
  std::ofstream(blocked_source, std::ios::binary) << "blocked";
  cockpit::recording::RecordingSessionLimits path_limits;
  path_limits.min_free_bytes = 1;
  path_limits.allowed_data_root = allowed_root;
  cockpit::recording::RecordingSession restricted_session(root / "restricted", "test_vehicle", {},
                                                          path_limits);
  cockpit::recording::RecordingDataFile blocked_file = copied_data_file;
  blocked_file.path = blocked_source.string();
  const auto blocked_link = allowed_root / "blocked-link.bin";
  std::filesystem::create_symlink(blocked_source, blocked_link);
  cockpit::recording::RecordingDataFile linked_file = copied_data_file;
  linked_file.path = blocked_link.string();
  std::string path_error;
  const bool path_limit_result =
      Check(restricted_session.Start("path_limit", &path_error),
            "restricted recording session did not start") &&
      Check(!restricted_session.AppendDataFile(blocked_file, &path_error),
            "recording copied a file outside the allowlist") &&
      Check(path_error.find("outside the allowed directory") != std::string::npos,
            "recording allowlist error was not diagnostic") &&
      Check(!restricted_session.AppendDataFile(linked_file, &path_error),
            "recording followed a symbolic link inside the allowlist") &&
      Check(restricted_session.Stop(&path_error), "restricted recording session did not stop");
  const bool result =
      Check(status.state == cockpit::recording::RecordingState::kIdle,
            "recording session did not return to idle") &&
      Check(status.messages_written == 5, "recording message count mismatch") &&
      Check(status.data_files_indexed == 2, "recording data file index count mismatch") &&
      Check(std::filesystem::exists(directory / "COMPLETE"), "recording COMPLETE marker missing") &&
      Check(manifest.find("\"state\": \"complete\"") != std::string::npos,
            "recording manifest state mismatch") &&
      Check(manifest.find("\"messages_written\": 5") != std::string::npos,
            "recording manifest message count mismatch") &&
      Check(manifest.find("\"data_files_indexed\": 2") != std::string::npos,
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
      Check(data_files.find("\"path\":\"artifacts/2_captured_photo.jpg\"") != std::string::npos,
            "copied camera data file path missing") &&
      Check(data_files.find("\"copied_into_session\":true") != std::string::npos,
            "copied camera data file flag missing") &&
      Check(data_files.find("\"checksum\":\"fnv1a64:") != std::string::npos,
            "copied camera data file checksum missing") &&
      Check(std::filesystem::exists(copied_artifact), "copied camera artifact missing") &&
      Check(ReadFile(copied_artifact) == "jpeg-test-data", "copied camera artifact mismatch") &&
      Check(recovered == 1, "interrupted recording recovery count mismatch") &&
      Check(std::filesystem::exists(interrupted_directory / "INTERRUPTED"),
            "interrupted recording marker missing") &&
      byte_limit_result && duration_limit_result && path_limit_result;
  std::filesystem::remove_all(root);
  return result ? 0 : 1;
}
