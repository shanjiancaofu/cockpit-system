#include "cockpit/modules/recording/recording_session.h"

#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
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

bool SyncPath(const std::filesystem::path& path, int flags, std::string* error) {
  const int descriptor = open(path.c_str(), flags | O_CLOEXEC);
  if (descriptor < 0) {
    AssignError(error, "open recording path for sync failed: " + path.string() + ": " +
                           std::strerror(errno));
    return false;
  }
  if (fsync(descriptor) != 0) {
    const int sync_error = errno;
    close(descriptor);
    AssignError(error,
                "sync recording path failed: " + path.string() + ": " + std::strerror(sync_error));
    return false;
  }
  if (close(descriptor) != 0) {
    AssignError(error,
                "close recording sync path failed: " + path.string() + ": " + std::strerror(errno));
    return false;
  }
  return true;
}

}  // namespace

RecordingSession::RecordingSession(std::filesystem::path root_directory, std::string vehicle_id,
                                   RecordingMetadata metadata, RecordingSessionLimits limits)
    : root_directory_(std::move(root_directory)),
      vehicle_id_(std::move(vehicle_id)),
      metadata_(std::move(metadata)),
      limits_(std::move(limits)) {
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
    active_bytes_ = 0;
    vehicle_messages_since_flush_ = 0;
    event_messages_since_flush_ = 0;
    data_messages_since_flush_ = 0;
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
  const std::string line = state.ToJson();
  if (!EnsureCapacity(line.size() + 1U, error)) {
    return false;
  }
  vehicle_state_file_ << line << '\n';
  if (!vehicle_state_file_) {
    SetError("write vehicle_state.jsonl failed");
    AssignError(error, status_.last_error);
    return false;
  }
  active_bytes_ += line.size() + 1U;
  ++status_.messages_written;
  ++vehicle_messages_since_flush_;
  if (status_.first_message_timestamp_ms == 0) {
    status_.first_message_timestamp_ms = state.timestamp_ms;
  }
  status_.last_message_timestamp_ms = state.timestamp_ms;
  if (vehicle_messages_since_flush_ >= kFlushInterval) {
    vehicle_state_file_.flush();
    vehicle_messages_since_flush_ = 0;
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
  const std::string line = event.ToJson();
  if (!EnsureCapacity(line.size() + 1U, error)) {
    return false;
  }
  event_file_ << line << '\n';
  if (!event_file_) {
    SetError("write events.jsonl failed");
    AssignError(error, status_.last_error);
    return false;
  }
  active_bytes_ += line.size() + 1U;
  ++status_.messages_written;
  ++event_messages_since_flush_;
  if (status_.first_message_timestamp_ms == 0) {
    status_.first_message_timestamp_ms = event.timestamp_ms;
  }
  status_.last_message_timestamp_ms = event.timestamp_ms;
  if (event_messages_since_flush_ >= kFlushInterval) {
    event_file_.flush();
    event_messages_since_flush_ = 0;
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
  const std::string line = recorded_file.ToJson();
  if (!EnsureCapacity(line.size() + 1U, error)) {
    return false;
  }
  data_file_index_ << line << '\n';
  if (!data_file_index_) {
    SetError("write data_files.jsonl failed");
    AssignError(error, status_.last_error);
    return false;
  }
  active_bytes_ += line.size() + 1U;
  ++status_.messages_written;
  ++status_.data_files_indexed;
  ++data_messages_since_flush_;
  if (status_.first_message_timestamp_ms == 0) {
    status_.first_message_timestamp_ms = recorded_file.timestamp_ms;
  }
  status_.last_message_timestamp_ms = recorded_file.timestamp_ms;
  if (data_messages_since_flush_ >= kFlushInterval) {
    data_file_index_.flush();
    data_messages_since_flush_ = 0;
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
    for (const char* filename : {"vehicle_state.jsonl", "events.jsonl", "data_files.jsonl"}) {
      if (!SyncPath(temporary_directory_ / filename, O_RDONLY, &close_error)) {
        SetError(close_error);
        AssignError(error, status_.last_error);
        return false;
      }
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
    if (!SyncPath(temporary_directory_, O_RDONLY | O_DIRECTORY, error)) {
      SetError(error == nullptr ? "sync recording directory failed" : *error);
      return false;
    }
    std::filesystem::rename(temporary_directory_, final_directory_);
    if (!SyncPath(final_directory_.parent_path(), O_RDONLY | O_DIRECTORY, error)) {
      SetError(error == nullptr ? "sync recording parent directory failed" : *error);
      return false;
    }
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

void RecordingSession::SetExistingBytes(std::uint64_t existing_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state != RecordingState::kRecording) {
    existing_bytes_ = existing_bytes;
  }
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
      if (!SyncPath(sessions_directory, O_RDONLY | O_DIRECTORY, &marker_error)) {
        AssignError(error, marker_error);
        return recovered;
      }
      ++recovered;
    }
    return recovered;
  } catch (const std::exception& exception) {
    AssignError(error, exception.what());
    return recovered;
  }
}

bool RecordingSession::EnsureCapacity(std::uint64_t additional_bytes, std::string* error) {
  const std::int64_t now_ms = time::NowMs();
  if (limits_.max_duration_ms > 0 && status_.started_at_ms > 0 && now_ms > status_.started_at_ms &&
      static_cast<std::uint64_t>(now_ms - status_.started_at_ms) > limits_.max_duration_ms) {
    SetError("recording session duration limit reached");
    AssignError(error, status_.last_error);
    return false;
  }
  if (limits_.max_session_bytes > 0 &&
      (active_bytes_ > limits_.max_session_bytes ||
       additional_bytes > limits_.max_session_bytes - active_bytes_)) {
    SetError("recording session byte limit reached");
    AssignError(error, status_.last_error);
    return false;
  }
  if (limits_.max_total_bytes > 0 &&
      (existing_bytes_ > limits_.max_total_bytes ||
       active_bytes_ > limits_.max_total_bytes - existing_bytes_ ||
       additional_bytes > limits_.max_total_bytes - existing_bytes_ - active_bytes_)) {
    SetError("recording total byte limit reached");
    AssignError(error, status_.last_error);
    return false;
  }

  struct statvfs filesystem_status {};
  const std::filesystem::path capacity_path =
      temporary_directory_.empty() ? root_directory_ : temporary_directory_;
  if (statvfs(capacity_path.c_str(), &filesystem_status) != 0) {
    SetError("read recording filesystem capacity failed: " + std::string(std::strerror(errno)));
    AssignError(error, status_.last_error);
    return false;
  }
  const std::uint64_t block_size = static_cast<std::uint64_t>(filesystem_status.f_frsize);
  const std::uint64_t available_blocks = static_cast<std::uint64_t>(filesystem_status.f_bavail);
  const std::uint64_t available_bytes =
      block_size > 0 && available_blocks > std::numeric_limits<std::uint64_t>::max() / block_size
          ? std::numeric_limits<std::uint64_t>::max()
          : block_size * available_blocks;
  if (available_bytes < additional_bytes ||
      available_bytes - additional_bytes < limits_.min_free_bytes) {
    SetError("recording filesystem free-space reserve reached");
    AssignError(error, status_.last_error);
    return false;
  }
  return true;
}

int RecordingSession::OpenAllowedDataFile(const std::filesystem::path& source, std::uint64_t* size,
                                          std::string* error) const {
  if (size == nullptr) {
    AssignError(error, "recording data file size result must not be null");
    return -1;
  }
  std::error_code filesystem_error;
  const std::filesystem::path allowed_root = std::filesystem::canonical(
      limits_.allowed_data_root.empty() ? root_directory_.parent_path() : limits_.allowed_data_root,
      filesystem_error);
  if (filesystem_error) {
    AssignError(error,
                "resolve recording data-file allowlist failed: " + filesystem_error.message());
    return -1;
  }
  const std::filesystem::path absolute_source =
      std::filesystem::absolute(source).lexically_normal();
  const std::filesystem::path relative = absolute_source.lexically_relative(allowed_root);
  if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..") {
    AssignError(error, "recording data file is outside the allowed directory: " + source.string());
    return -1;
  }

  const int root_fd = open(allowed_root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
  if (root_fd < 0) {
    AssignError(error,
                "open recording data-file allowlist failed: " + std::string(std::strerror(errno)));
    return -1;
  }
  struct open_how how {};
  how.flags = O_RDONLY | O_CLOEXEC;
  how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS;
  const int source_fd =
      static_cast<int>(syscall(SYS_openat2, root_fd, relative.c_str(), &how, sizeof(how)));
  const int open_error = errno;
  close(root_fd);
  if (source_fd < 0) {
    AssignError(error, "open recording data file within allowlist failed: " +
                           std::string(std::strerror(open_error)));
    return -1;
  }
  struct stat source_status {};
  if (fstat(source_fd, &source_status) != 0) {
    const int status_error = errno;
    close(source_fd);
    AssignError(error, "read recording data file status failed: " +
                           std::string(std::strerror(status_error)));
    return -1;
  }
  if (!S_ISREG(source_status.st_mode) || source_status.st_size < 0) {
    close(source_fd);
    AssignError(error, "recording data file must be a regular file");
    return -1;
  }
  *size = static_cast<std::uint64_t>(source_status.st_size);
  return source_fd;
}

bool RecordingSession::CopyDataFile(RecordingDataFile* file, std::string* error) {
  const std::filesystem::path source(file->path);
  std::uint64_t source_size = 0;
  const int source_fd = OpenAllowedDataFile(source, &source_size, error);
  if (source_fd < 0) {
    return false;
  }
  if (!EnsureCapacity(source_size, error)) {
    close(source_fd);
    return false;
  }
  const std::string filename = source.filename().string();
  if (filename.empty() || filename == "." || filename == "..") {
    close(source_fd);
    AssignError(error, "recording data file has an invalid filename: " + file->path);
    return false;
  }

  std::error_code filesystem_error;
  const std::filesystem::path artifact_directory = temporary_directory_ / "artifacts";
  std::filesystem::create_directories(artifact_directory, filesystem_error);
  if (filesystem_error) {
    close(source_fd);
    AssignError(error, "create recording artifact directory failed: " + filesystem_error.message());
    return false;
  }
  const std::filesystem::path relative_path =
      std::filesystem::path("artifacts") /
      (std::to_string(status_.data_files_indexed + 1U) + "_" + filename);
  const std::filesystem::path destination = temporary_directory_ / relative_path;
  const int destination_fd =
      open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (destination_fd < 0) {
    close(source_fd);
    AssignError(error, "create recording artifact failed: " + std::string(std::strerror(errno)));
    return false;
  }

  bool copied = true;
  std::string copy_failure;
  std::uint64_t remaining = source_size;
  char buffer[64 * 1024];
  while (remaining > 0) {
    const std::size_t requested =
        static_cast<std::size_t>(std::min<std::uint64_t>(remaining, sizeof(buffer)));
    ssize_t read_size = read(source_fd, buffer, requested);
    if (read_size < 0 && errno == EINTR) {
      continue;
    }
    if (read_size < 0) {
      copy_failure = std::strerror(errno);
      copied = false;
      break;
    }
    if (read_size == 0) {
      copy_failure = "source file changed during copy";
      copied = false;
      break;
    }
    ssize_t written = 0;
    while (written < read_size) {
      const ssize_t write_size =
          write(destination_fd, buffer + written, static_cast<std::size_t>(read_size - written));
      if (write_size < 0 && errno == EINTR) {
        continue;
      }
      if (write_size <= 0) {
        copy_failure = write_size < 0 ? std::strerror(errno) : "zero-byte artifact write";
        copied = false;
        break;
      }
      written += write_size;
    }
    if (!copied) {
      break;
    }
    remaining -= static_cast<std::uint64_t>(read_size);
  }
  if (copied && fsync(destination_fd) != 0) {
    copy_failure = std::strerror(errno);
    copied = false;
  }
  close(source_fd);
  if (close(destination_fd) != 0 && copied) {
    copy_failure = std::strerror(errno);
    copied = false;
  }
  if (!copied) {
    std::error_code remove_error;
    std::filesystem::remove(destination, remove_error);
    AssignError(error, "copy recording data file failed: " + copy_failure);
    return false;
  }
  active_bytes_ += source_size;
  file->path = relative_path.generic_string();
  file->size_bytes = source_size;
  if (file->checksum.empty() && !ComputeFnv1a64(destination, &file->checksum, error)) {
    std::error_code remove_error;
    std::filesystem::remove(destination, remove_error);
    active_bytes_ -= source_size;
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
  return SyncPath(path, O_RDONLY, error);
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
  return SyncPath(directory / "manifest.json", O_RDONLY, error);
}

void RecordingSession::SetError(const std::string& error) {
  status_.state = RecordingState::kFaulted;
  status_.last_error = error;
}

}  // namespace recording
}  // namespace cockpit
