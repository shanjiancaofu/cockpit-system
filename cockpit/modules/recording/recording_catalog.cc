#include "cockpit/modules/recording/recording_catalog.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <exception>
#include <system_error>
#include <utility>

namespace cockpit {
namespace recording {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool IsManagedSession(const std::filesystem::path& directory) {
  return std::filesystem::exists(directory / "COMPLETE") ||
         std::filesystem::exists(directory / "INTERRUPTED");
}

}  // namespace

RecordingCatalog::RecordingCatalog(std::filesystem::path root_directory)
    : root_directory_(std::move(root_directory)) {
}

bool RecordingCatalog::Refresh(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return RefreshLocked(error);
}

std::vector<RecordingSessionInfo> RecordingCatalog::List(std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (limit == 0 || limit >= sessions_.size()) {
    return sessions_;
  }
  return {sessions_.begin(), sessions_.begin() + static_cast<std::ptrdiff_t>(limit)};
}

bool RecordingCatalog::Delete(const std::string& session_id, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto entry = std::find_if(sessions_.begin(), sessions_.end(),
                                  [&session_id](const RecordingSessionInfo& info) {
                                    return info.session_id == session_id;
                                  });
  if (entry == sessions_.end()) {
    AssignError(error, "recording session not found: " + session_id);
    return false;
  }
  std::error_code filesystem_error;
  std::filesystem::remove_all(entry->directory, filesystem_error);
  if (filesystem_error) {
    AssignError(error, "delete recording session failed: " + filesystem_error.message());
    return false;
  }
  sessions_.erase(entry);
  return true;
}

bool RecordingCatalog::Prune(const RecordingRetentionPolicy& policy, RecordingPruneResult* result,
                             std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  RecordingPruneResult local_result;
  std::uint64_t bytes = 0;
  for (const auto& session : sessions_) {
    bytes += session.size_bytes;
  }
  while (!sessions_.empty() &&
         ((policy.max_sessions > 0 && sessions_.size() > policy.max_sessions) ||
          (policy.max_total_bytes > 0 && bytes > policy.max_total_bytes))) {
    const auto oldest = std::prev(sessions_.end());
    std::error_code filesystem_error;
    std::filesystem::remove_all(oldest->directory, filesystem_error);
    if (filesystem_error) {
      AssignError(error, "prune recording session failed: " + filesystem_error.message());
      return false;
    }
    bytes -= oldest->size_bytes;
    local_result.bytes_deleted += oldest->size_bytes;
    ++local_result.sessions_deleted;
    sessions_.erase(oldest);
  }
  if (result != nullptr) {
    *result = local_result;
  }
  return true;
}

std::uint64_t RecordingCatalog::total_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::uint64_t bytes = 0;
  for (const auto& session : sessions_) {
    bytes += session.size_bytes;
  }
  return bytes;
}

bool RecordingCatalog::ReadSession(const std::filesystem::path& directory,
                                   RecordingSessionInfo* info, std::string* error) {
  try {
    const YAML::Node manifest = YAML::LoadFile((directory / "manifest.json").string());
    info->session_id = manifest["session_id"].as<std::string>();
    info->state = std::filesystem::exists(directory / "INTERRUPTED")
                      ? "interrupted"
                      : manifest["state"].as<std::string>();
    info->trigger = manifest["trigger"].as<std::string>("unknown");
    info->directory = directory.string();
    info->messages_written = manifest["messages_written"].as<std::uint64_t>(0);
    info->started_at_ms = manifest["started_at_ms"].as<std::int64_t>(0);
    info->stopped_at_ms = manifest["stopped_at_ms"].as<std::int64_t>(0);
    info->size_bytes = DirectorySize(directory);
    return true;
  } catch (const std::exception& exception) {
    AssignError(error,
                "read recording manifest failed: " + directory.string() + ": " + exception.what());
    return false;
  }
}

std::uint64_t RecordingCatalog::DirectorySize(const std::filesystem::path& directory) {
  std::uint64_t bytes = 0;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(directory, error), end;
       iterator != end && !error; iterator.increment(error)) {
    if (iterator->is_regular_file(error)) {
      bytes += iterator->file_size(error);
    }
  }
  return bytes;
}

bool RecordingCatalog::RefreshLocked(std::string* error) {
  const std::filesystem::path sessions_directory = root_directory_ / "sessions";
  std::vector<RecordingSessionInfo> discovered;
  try {
    std::filesystem::create_directories(sessions_directory);
    for (const auto& entry : std::filesystem::directory_iterator(sessions_directory)) {
      if (!entry.is_directory() || !IsManagedSession(entry.path())) {
        continue;
      }
      RecordingSessionInfo info;
      if (!ReadSession(entry.path(), &info, error)) {
        info.session_id = entry.path().filename().string();
        info.state = "corrupted";
        info.trigger = "unknown";
        info.directory = entry.path().string();
        info.size_bytes = DirectorySize(entry.path());
        if (error != nullptr) {
          error->clear();
        }
      }
      discovered.push_back(std::move(info));
    }
  } catch (const std::exception& exception) {
    AssignError(error, exception.what());
    return false;
  }
  std::sort(discovered.begin(), discovered.end(),
            [](const RecordingSessionInfo& left, const RecordingSessionInfo& right) {
              return left.started_at_ms > right.started_at_ms;
            });
  sessions_ = std::move(discovered);
  return true;
}

}  // namespace recording
}  // namespace cockpit
