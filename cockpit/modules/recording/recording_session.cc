#include "cockpit/modules/recording/recording_session.h"

#include <unistd.h>

#include <atomic>
#include <exception>
#include <iomanip>
#include <sstream>
#include <utility>

#include "cockpit/core/utils/Time.h"

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

std::string EscapeJson(const std::string& input) {
  std::ostringstream output;
  for (const char character : input) {
    switch (character) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << character;
        break;
    }
  }
  return output.str();
}

std::string MakeSessionId() {
  return std::to_string(utils::NowMs()) + "_" + std::to_string(getpid()) + "_" +
         std::to_string(g_session_sequence.fetch_add(1U));
}

}  // namespace

RecordingSession::RecordingSession(std::filesystem::path root_directory, std::string vehicle_id)
    : root_directory_(std::move(root_directory)), vehicle_id_(std::move(vehicle_id)) {
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
    status_.started_at_ms = utils::NowMs();
    temporary_directory_ = sessions_directory / (".recording_" + status_.session_id);
    final_directory_ = sessions_directory / status_.session_id;
    std::filesystem::create_directory(temporary_directory_);
    status_.directory = temporary_directory_.string();

    vehicle_state_file_.open(temporary_directory_ / "vehicle_state.jsonl",
                             std::ios::out | std::ios::app);
    if (!vehicle_state_file_.is_open()) {
      throw std::runtime_error("open vehicle_state.jsonl failed");
    }
    if (!WriteManifest(temporary_directory_, "recording", error)) {
      vehicle_state_file_.close();
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

bool RecordingSession::Stop(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state == RecordingState::kIdle) {
    return true;
  }
  if (status_.state == RecordingState::kFaulted) {
    if (vehicle_state_file_.is_open()) {
      vehicle_state_file_.flush();
      vehicle_state_file_.close();
    }
    AssignError(error, status_.last_error);
    return false;
  }

  try {
    vehicle_state_file_.flush();
    vehicle_state_file_.close();
    status_.stopped_at_ms = utils::NowMs();
    if (!WriteManifest(temporary_directory_, "complete", error)) {
      status_.state = RecordingState::kFaulted;
      return false;
    }
    std::filesystem::rename(temporary_directory_, final_directory_);
    std::ofstream complete_file(final_directory_ / "COMPLETE");
    complete_file << status_.stopped_at_ms << '\n';
    complete_file.close();
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
  if (!std::filesystem::exists(sessions_directory)) {
    return 0;
  }
  std::size_t recovered = 0;
  try {
    for (const auto& entry : std::filesystem::directory_iterator(sessions_directory)) {
      const std::string name = entry.path().filename().string();
      if (!entry.is_directory() || name.rfind(".recording_", 0) != 0) {
        continue;
      }
      const std::filesystem::path destination =
          sessions_directory / ("interrupted_" + name.substr(std::string(".recording_").size()));
      std::filesystem::rename(entry.path(), destination);
      std::ofstream marker(destination / "INTERRUPTED");
      marker << utils::NowMs() << '\n';
      ++recovered;
    }
    return recovered;
  } catch (const std::exception& exception) {
    AssignError(error, exception.what());
    return recovered;
  }
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
           << "  \"session_id\": \"" << EscapeJson(status_.session_id) << "\",\n"
           << "  \"vehicle_id\": \"" << EscapeJson(vehicle_id_) << "\",\n"
           << "  \"state\": \"" << EscapeJson(state) << "\",\n"
           << "  \"trigger\": \"" << EscapeJson(status_.trigger) << "\",\n"
           << "  \"started_at_ms\": " << status_.started_at_ms << ",\n"
           << "  \"stopped_at_ms\": " << status_.stopped_at_ms << ",\n"
           << "  \"messages_written\": " << status_.messages_written << ",\n"
           << "  \"first_message_timestamp_ms\": " << status_.first_message_timestamp_ms << ",\n"
           << "  \"last_message_timestamp_ms\": " << status_.last_message_timestamp_ms << "\n"
           << "}\n";
  if (!manifest) {
    AssignError(error, "write manifest.json failed");
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
