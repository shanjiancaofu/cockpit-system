#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "cockpit/modules/recording/recording_event.h"
#include "cockpit/modules/vehicle/VehicleState.h"

namespace cockpit {
namespace recording {

enum class RecordingState {
  kIdle,
  kRecording,
  kFaulted,
};

struct RecordingStatus {
  RecordingState state = RecordingState::kIdle;
  std::string session_id;
  std::string directory;
  std::string trigger;
  std::uint64_t messages_written = 0;
  std::int64_t started_at_ms = 0;
  std::int64_t stopped_at_ms = 0;
  std::int64_t first_message_timestamp_ms = 0;
  std::int64_t last_message_timestamp_ms = 0;
  std::uint64_t stored_sessions = 0;
  std::uint64_t stored_bytes = 0;
  std::string last_error;
};

struct RecordingMetadata {
  std::string project = "cockpit-system";
  std::string schema_version = "1";
  std::string config_path;
  std::vector<std::string> sources = {"vehicle_state", "events"};
};

class RecordingSession {
 public:
  RecordingSession(std::filesystem::path root_directory, std::string vehicle_id,
                   RecordingMetadata metadata = {});
  ~RecordingSession();

  RecordingSession(const RecordingSession&) = delete;
  RecordingSession& operator=(const RecordingSession&) = delete;

  bool Start(const std::string& trigger, std::string* error);
  bool Append(const vehicle::VehicleState& state, std::string* error);
  bool AppendEvent(const RecordingEvent& event, std::string* error);
  bool Stop(std::string* error);
  RecordingStatus status() const;

  static std::size_t RecoverInterrupted(const std::filesystem::path& root_directory,
                                        std::string* error);

 private:
  bool WriteManifest(const std::filesystem::path& directory, const std::string& state,
                     std::string* error) const;
  void SetError(const std::string& error);

  const std::filesystem::path root_directory_;
  const std::string vehicle_id_;
  const RecordingMetadata metadata_;
  mutable std::mutex mutex_;
  RecordingStatus status_;
  std::filesystem::path temporary_directory_;
  std::filesystem::path final_directory_;
  std::ofstream vehicle_state_file_;
  std::ofstream event_file_;
};

}  // namespace recording
}  // namespace cockpit
