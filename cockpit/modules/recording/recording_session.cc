#include "cockpit/modules/recording/recording_session.h"

#include <unistd.h>

#include <atomic>
#include <exception>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#include "cockpit/core/json/json.h"
#include "cockpit/core/time/time.h"
#include "cockpit/modules/recording/file_checksum.h"

namespace cockpit {
namespace recording {
namespace {

constexpr std::uint64_t kFlushInterval = 10;
std::atomic<std::uint64_t> g_session_sequence{0};

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::string MakeSessionId() {
  return std::to_string(time::NowMs()) + "_" + std::to_string(getpid()) + "_" +
         std::to_string(g_session_sequence.fetch_add(1U));
}

}  // namespace

RecordingSession::RecordingSession(std::filesystem::path root_directory, std::string vehicle_id,
                                   RecordingMetadata metadata)
    : root_directory_(std::move(root_directory)),
      vehicle_id_(std::move(vehicle_id)),
      metadata_(std::move(metadata)) {
}

RecordingSession::~RecordingSession() {
  std::string error;
  Stop(&error);
}

bool RecordingSession::Start(const std::string& trigger, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state == RecordingState::kRecording) {
    AssignError(error, "recording session is already active");
    return false;
  }
  if (status_.state == RecordingState::kFaulted) {
    AssignError(error, "recording session is faulted: " + status_.last_error);
    return false;
  }

  try {
    const std::filesystem::path sessions_directory = root_directory_ / "sessions";
    std::filesystem::create_directories(sessions_directory);
    status_ = RecordingStatus{};
    status_.state = RecordingState::kRecording;
    status_.session_id = MakeSessionId();
    status_.trigger = trigger.empty() ? "manual" : trigger;
    status_.started_at_ms = time::NowMs();
    temporary_directory_ = sessions_directory / (".recording_" + status_.session_id);
    final_directory_ = sessions_directory / status_.session_id;
    std::filesystem::create_directory(temporary_directory_);
    status_.directory = temporary_directory_.string();

    vehicle_state_file_.open(temporary_directory_ / "vehicle_state.jsonl",
                             std::ios::out | std::ios::app);
    if (!vehicle_state_file_.is_open()) {
      throw std::runtime_error("open vehicle_state.jsonl failed");
    }
    event_file_.open(temporary_directory_ / "events.jsonl", std::ios::out | std::ios::app);
    if (!event_file_.is_open()) {
      throw std::runtime_error("open events.jsonl failed");
    }
    data_file_index_.open(temporary_directory_ / "data_files.jsonl", std::ios::out | std::ios::app);
    if (!data_file_index_.is_open()) {
      throw std::runtime_error("open data_files.jsonl failed");
    }
    if (!WriteManifest(temporary_directory_, "recording", error)) {
      vehicle_state_file_.close();
      event_file_.close();
      data_file_index_.close();
      status_.state = RecordingState::kFaulted;
      return false;
    }
    return true;
  } catch (const std::exception& exception) {
    SetError(exception.what());
    AssignError(error, status_.last_error);
    return false;
  }
}

bool RecordingSession::Append(const vehicle::VehicleState& state, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state != RecordingState::kRecording || !vehicle_state_file_.is_open()) {
    AssignError(error, "recording session is not active");
    return false;
  }
  vehicle_state_file_ << state.ToJson() << '\n';
  if (!vehicle_state_file_) {
    SetError("write vehicle_state.jsonl failed");
    AssignError(error, status_.last_error);
    return false;
  }
  ++status_.messages_written;
  if (status_.first_message_timestamp_ms == 0) {
    status_.first_message_timestamp_ms = state.timestamp_ms;
  }
  status_.last_message_timestamp_ms = state.timestamp_ms;
  if (status_.messages_written % kFlushInterval == 0) {
    vehicle_state_file_.flush();
  }
  return true;
}

bool RecordingSession::AppendEvent(const RecordingEvent& event, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state != RecordingState::kRecording || !event_file_.is_open()) {
    AssignError(error, "recording session is not active");
    return false;
  }
  if (!event.IsValid()) {
    AssignError(error, "recording event is invalid");
    return false;
  }
  event_file_ << event.ToJson() << '\n';
  if (!event_file_) {
    SetError("write events.jsonl failed");
    AssignError(error, status_.last_error);
    return false;
  }
  ++status_.messages_written;
  if (status_.first_message_timestamp_ms == 0) {
    status_.first_message_timestamp_ms = event.timestamp_ms;
  }
  status_.last_message_timestamp_ms = event.timestamp_ms;
  if (status_.messages_written % kFlushInterval == 0) {
    event_file_.flush();
  }
  return true;
}

bool RecordingSession::AppendDataFile(const RecordingDataFile& file, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state != RecordingState::kRecording || !data_file_index_.is_open()) {
    AssignError(error, "recording session is not active");
    return false;
  }
  if (!file.IsValid()) {
    AssignError(error, "recording data file index is invalid");
    return false;
  }
  RecordingDataFile recorded_file = file;
  if (recorded_file.copy_into_session && !CopyDataFile(&recorded_file, error)) {
    return false;
  }
  data_file_index_ << recorded_file.ToJson() << '\n';
  if (!data_file_index_) {
    SetError("write data_files.jsonl failed");
    AssignError(error, status_.last_error);
    return false;
  }
  ++status_.messages_written;
  ++status_.data_files_indexed;
  if (status_.first_message_timestamp_ms == 0) {
    status_.first_message_timestamp_ms = recorded_file.timestamp_ms;
  }
  status_.last_message_timestamp_ms = recorded_file.timestamp_ms;
  if (status_.messages_written % kFlushInterval == 0) {
    data_file_index_.flush();
  }
  return true;
}

bool RecordingSession::Stop(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state == RecordingState::kIdle) {
    return true;
  }
  if (status_.state == RecordingState::kFaulted) {
    std::string ignored_error;
    CloseOutput(&vehicle_state_file_, "vehicle_state.jsonl", &ignored_error);
    CloseOutput(&event_file_, "events.jsonl", &ignored_error);
    CloseOutput(&data_file_index_, "data_files.jsonl", &ignored_error);
    AssignError(error, status_.last_error);
    return false;
  }

  try {
    std::string close_error;
    bool outputs_closed = CloseOutput(&vehicle_state_file_, "vehicle_state.jsonl", &close_error);
    std::string current_error;
    if (!CloseOutput(&event_file_, "events.jsonl", &current_error) && outputs_closed) {
      outputs_closed = false;
      close_error = current_error;
    }
    current_error.clear();
    if (!CloseOutput(&data_file_index_, "data_files.jsonl", &current_error) && outputs_closed) {
      outputs_closed = false;
      close_error = current_error;
    }
    if (!outputs_closed) {
      SetError(close_error);
      AssignError(error, status_.last_error);
      return false;
    }
    status_.stopped_at_ms = time::NowMs();
    if (!WriteManifest(temporary_directory_, "complete", error)) {
      SetError(error == nullptr ? "write recording manifest failed" : *error);
      return false;
    }
    if (!WriteMarker(temporary_directory_ / "COMPLETE", status_.stopped_at_ms, error)) {
      SetError(error == nullptr ? "write COMPLETE marker failed" : *error);
      return false;
    }
    std::filesystem::rename(temporary_directory_, final_directory_);
    status_.state = RecordingState::kIdle;
    status_.directory = final_directory_.string();
    return true;
  } catch (const std::exception& exception) {
    SetError(exception.what());
    AssignError(error, status_.last_error);
    return false;
  }
}

RecordingStatus RecordingSession::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

std::size_t RecordingSession::RecoverInterrupted(const std::filesystem::path& root_directory,
                                                 std::string* error) {
  const std::filesystem::path sessions_directory = root_directory / "sessions";
  std::size_t recovered = 0;
  try {
    if (!std::filesystem::exists(sessions_directory)) {
      return 0;
    }
    for (const auto& entry : std::filesystem::directory_iterator(sessions_directory)) {
      const std::string name = entry.path().filename().string();
      if (!entry.is_directory() || name.rfind(".recording_", 0) != 0) {
        continue;
      }
      const std::filesystem::path destination =
          sessions_directory / ("interrupted_" + name.substr(std::string(".recording_").size()));
      std::string marker_error;
      if (!WriteMarker(entry.path() / "INTERRUPTED", time::NowMs(), &marker_error)) {
        AssignError(error, marker_error);
        return recovered;
      }
      std::filesystem::rename(entry.path(), destination);
      ++recovered;
    }
    return recovered;
  } catch (const std::exception& exception) {
    AssignError(error, exception.what());
    return recovered;
  }
}

bool RecordingSession::CopyDataFile(RecordingDataFile* file, std::string* error) {
  const std::filesystem::path source(file->path);
  std::error_code filesystem_error;
  if (!std::filesystem::is_regular_file(source, filesystem_error) || filesystem_error) {
    AssignError(error, "recording data file is not a readable regular file: " + file->path);
    return false;
  }
  const std::string filename = source.filename().string();
  if (filename.empty() || filename == "." || filename == "..") {
    AssignError(error, "recording data file has an invalid filename: " + file->path);
    return false;
  }

  const std::filesystem::path artifact_directory = temporary_directory_ / "artifacts";
  std::filesystem::create_directories(artifact_directory, filesystem_error);
  if (filesystem_error) {
    AssignError(error, "create recording artifact directory failed: " + filesystem_error.message());
    return false;
  }
  const std::filesystem::path relative_path =
      std::filesystem::path("artifacts") /
      (std::to_string(status_.data_files_indexed + 1U) + "_" + filename);
  const std::filesystem::path destination = temporary_directory_ / relative_path;
  const bool copied = std::filesystem::copy_file(
      source, destination, std::filesystem::copy_options::none, filesystem_error);
  if (!copied || filesystem_error) {
    AssignError(error,
                filesystem_error
                    ? "copy recording data file failed: " + filesystem_error.message()
                    : "recording artifact destination already exists: " + destination.string());
    return false;
  }
  const std::uintmax_t size = std::filesystem::file_size(destination, filesystem_error);
  if (filesystem_error || size > std::numeric_limits<std::uint64_t>::max()) {
    std::error_code remove_error;
    std::filesystem::remove(destination, remove_error);
    AssignError(error, filesystem_error ? "read copied recording data file size failed: " +
                                              filesystem_error.message()
                                        : "copied recording data file is too large");
    return false;
  }
  file->path = relative_path.generic_string();
  file->size_bytes = static_cast<std::uint64_t>(size);
  if (file->checksum.empty() && !ComputeFnv1a64(destination, &file->checksum, error)) {
    std::error_code remove_error;
    std::filesystem::remove(destination, remove_error);
    return false;
  }
  return true;
}

bool RecordingSession::CloseOutput(std::ofstream* output, const char* filename,
                                   std::string* error) {
  if (!output->is_open()) {
    return true;
  }
  output->flush();
  if (!*output) {
    output->close();
    AssignError(error, std::string("flush ") + filename + " failed");
    return false;
  }
  output->close();
  if (output->fail()) {
    AssignError(error, std::string("close ") + filename + " failed");
    return false;
  }
  return true;
}

bool RecordingSession::WriteMarker(const std::filesystem::path& path, std::int64_t timestamp_ms,
                                   std::string* error) {
  std::ofstream marker(path, std::ios::out | std::ios::trunc);
  if (!marker.is_open()) {
    AssignError(error, "open recording marker failed: " + path.string());
    return false;
  }
  marker << timestamp_ms << '\n';
  marker.flush();
  if (!marker) {
    marker.close();
    AssignError(error, "write recording marker failed: " + path.string());
    return false;
  }
  marker.close();
  if (marker.fail()) {
    AssignError(error, "close recording marker failed: " + path.string());
    return false;
  }
  return true;
}

bool RecordingSession::WriteManifest(const std::filesystem::path& directory,
                                     const std::string& state, std::string* error) const {
  std::ofstream manifest(directory / "manifest.json", std::ios::out | std::ios::trunc);
  if (!manifest.is_open()) {
    AssignError(error, "open manifest.json failed");
    return false;
  }
  manifest << "{\n"
           << "  \"version\": 1,\n"
           << "  \"project\": \"" << json::EscapeString(metadata_.project) << "\",\n"
           << "  \"schema_version\": \"" << json::EscapeString(metadata_.schema_version) << "\",\n"
           << "  \"session_id\": \"" << json::EscapeString(status_.session_id) << "\",\n"
           << "  \"vehicle_id\": \"" << json::EscapeString(vehicle_id_) << "\",\n"
           << "  \"config_path\": \"" << json::EscapeString(metadata_.config_path) << "\",\n"
           << "  \"config_checksum\": \"" << json::EscapeString(metadata_.config_checksum)
           << "\",\n"
           << "  \"git_commit\": \"" << json::EscapeString(metadata_.git_commit) << "\",\n"
           << "  \"git_dirty\": " << (metadata_.git_dirty ? "true" : "false") << ",\n"
           << "  \"build_type\": \"" << json::EscapeString(metadata_.build_type) << "\",\n"
           << "  \"binary_version\": \"" << json::EscapeString(metadata_.binary_version) << "\",\n"
           << "  \"sources\": [";
  for (std::size_t i = 0; i < metadata_.sources.size(); ++i) {
    if (i > 0) {
      manifest << ", ";
    }
    manifest << "\"" << json::EscapeString(metadata_.sources[i]) << "\"";
  }
  manifest << "],\n"
           << "  \"state\": \"" << json::EscapeString(state) << "\",\n"
           << "  \"trigger\": \"" << json::EscapeString(status_.trigger) << "\",\n"
           << "  \"started_at_ms\": " << status_.started_at_ms << ",\n"
           << "  \"stopped_at_ms\": " << status_.stopped_at_ms << ",\n"
           << "  \"messages_written\": " << status_.messages_written << ",\n"
           << "  \"data_files_indexed\": " << status_.data_files_indexed << ",\n"
           << "  \"first_message_timestamp_ms\": " << status_.first_message_timestamp_ms << ",\n"
           << "  \"last_message_timestamp_ms\": " << status_.last_message_timestamp_ms << "\n"
           << "}\n";
  if (!manifest) {
    AssignError(error, "write manifest.json failed");
    return false;
  }
  manifest.flush();
  if (!manifest) {
    AssignError(error, "flush manifest.json failed");
    return false;
  }
  manifest.close();
  if (manifest.fail()) {
    AssignError(error, "close manifest.json failed");
    return false;
  }
  return true;
}

void RecordingSession::SetError(const std::string& error) {
  status_.state = RecordingState::kFaulted;
  status_.last_error = error;
}

}  // namespace recording
}  // namespace cockpit
