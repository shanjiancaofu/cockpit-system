#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace cockpit {
namespace recording {

struct RecordingSessionInfo {
  std::string session_id;
  std::string state;
  std::string trigger;
  std::string directory;
  std::uint64_t messages_written = 0;
  std::uint64_t size_bytes = 0;
  std::int64_t started_at_ms = 0;
  std::int64_t stopped_at_ms = 0;
};

struct RecordingRetentionPolicy {
  std::size_t max_sessions = 100;
  std::uint64_t max_total_bytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;
};

struct RecordingPruneResult {
  std::size_t sessions_deleted = 0;
  std::uint64_t bytes_deleted = 0;
};

class RecordingCatalog {
 public:
  explicit RecordingCatalog(std::filesystem::path root_directory);

  bool Refresh(std::string* error);
  std::vector<RecordingSessionInfo> List(std::size_t limit = 0) const;
  bool Delete(const std::string& session_id, std::string* error);
  bool Prune(const RecordingRetentionPolicy& policy, RecordingPruneResult* result,
             std::string* error);
  std::uint64_t total_bytes() const;

 private:
  static bool ReadSession(const std::filesystem::path& directory, RecordingSessionInfo* info,
                          std::string* error);
  static std::uint64_t DirectorySize(const std::filesystem::path& directory);
  bool RefreshLocked(std::string* error);

  const std::filesystem::path root_directory_;
  mutable std::mutex mutex_;
  std::vector<RecordingSessionInfo> sessions_;
};

}  // namespace recording
}  // namespace cockpit
