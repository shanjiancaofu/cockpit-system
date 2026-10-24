#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cockpit {
namespace recording {

enum class RecordingTimelineEntryKind {
  kVehicleState,
  kEvent,
  kDataFile,
};

struct RecordingTimelineEntry {
  std::int64_t timestamp_ms = 0;
  RecordingTimelineEntryKind kind = RecordingTimelineEntryKind::kEvent;
  std::string source;
  std::string label;
  std::string path;
  std::string record_json;
};

struct RecordingTimelineQuery {
  std::int64_t from_timestamp_ms = 0;
  std::int64_t to_timestamp_ms = 0;
  std::size_t limit = 100;
};

struct RecordingTimelineResult {
  std::vector<RecordingTimelineEntry> entries;
  std::uint64_t total_entries = 0;
  std::uint64_t corrupted_lines = 0;
  bool truncated = false;
};

class RecordingTimelineReader {
 public:
  static constexpr std::size_t kDefaultLimit = 100;
  static constexpr std::size_t kMaximumLimit = 1000;

  static bool Read(const std::filesystem::path& session_directory,
                   const RecordingTimelineQuery& query, RecordingTimelineResult* result,
                   std::string* error);
};

const char* ToString(RecordingTimelineEntryKind kind);

}  // namespace recording
}  // namespace cockpit
