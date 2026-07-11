#include "cockpit/modules/recording/recording_timeline.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <utility>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace recording {
namespace {

constexpr std::size_t kMaximumLineBytes = 1024U * 1024U;

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool InRange(std::int64_t timestamp_ms, const RecordingTimelineQuery& query) {
  return timestamp_ms >= query.from_timestamp_ms &&
         (query.to_timestamp_ms == 0 || timestamp_ms <= query.to_timestamp_ms);
}

bool ParseEntry(const std::string& line, RecordingTimelineEntryKind kind,
                RecordingTimelineEntry* entry) {
  if (line.size() > kMaximumLineBytes || !json::IsValidValue(line)) {
    return false;
  }
  try {
    const YAML::Node node = YAML::Load(line);
    if (!node.IsMap() || !node["timestamp_ms"]) {
      return false;
    }
    entry->timestamp_ms = node["timestamp_ms"].as<std::int64_t>();
    if (entry->timestamp_ms <= 0) {
      return false;
    }
    entry->kind = kind;
    entry->record_json = line;
    switch (kind) {
      case RecordingTimelineEntryKind::kVehicleState:
        entry->source = node["source"].as<std::string>("vehicle");
        entry->label = "vehicle_state";
        break;
      case RecordingTimelineEntryKind::kEvent:
        entry->source = "event";
        entry->label = node["topic"].as<std::string>();
        if (entry->label.empty()) {
          return false;
        }
        break;
      case RecordingTimelineEntryKind::kDataFile:
        entry->source = node["source"].as<std::string>();
        entry->label = node["kind"].as<std::string>();
        entry->path = node["path"].as<std::string>();
        if (entry->source.empty() || entry->label.empty() || entry->path.empty()) {
          return false;
        }
        break;
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool ReadFile(const std::filesystem::path& path, RecordingTimelineEntryKind kind,
              const RecordingTimelineQuery& query, RecordingTimelineResult* result,
              std::string* error) {
  std::error_code filesystem_error;
  if (!std::filesystem::exists(path, filesystem_error)) {
    if (filesystem_error) {
      AssignError(error, "inspect recording timeline file failed: " + path.string() + ": " +
                             filesystem_error.message());
      return false;
    }
    return true;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    AssignError(error, "open recording timeline file failed: " + path.string());
    return false;
  }
  std::string line;
  while (std::getline(input, line)) {
    RecordingTimelineEntry entry;
    if (!ParseEntry(line, kind, &entry)) {
      ++result->corrupted_lines;
      continue;
    }
    if (InRange(entry.timestamp_ms, query)) {
      result->entries.push_back(std::move(entry));
    }
  }
  if (input.bad()) {
    AssignError(error, "read recording timeline file failed: " + path.string());
    return false;
  }
  return true;
}

}  // namespace

bool RecordingTimelineReader::Read(const std::filesystem::path& session_directory,
                                   const RecordingTimelineQuery& query,
                                   RecordingTimelineResult* result, std::string* error) {
  if (result == nullptr) {
    AssignError(error, "recording timeline result must not be null");
    return false;
  }
  if (query.from_timestamp_ms < 0 || query.to_timestamp_ms < 0 ||
      (query.to_timestamp_ms > 0 && query.from_timestamp_ms > query.to_timestamp_ms)) {
    AssignError(error, "recording timeline timestamp range is invalid");
    return false;
  }
  if (query.limit == 0 || query.limit > kMaximumLimit) {
    AssignError(error,
                "recording timeline limit must be between 1 and " + std::to_string(kMaximumLimit));
    return false;
  }

  RecordingTimelineResult local_result;
  if (!ReadFile(session_directory / "vehicle_state.jsonl",
                RecordingTimelineEntryKind::kVehicleState, query, &local_result, error) ||
      !ReadFile(session_directory / "events.jsonl", RecordingTimelineEntryKind::kEvent, query,
                &local_result, error) ||
      !ReadFile(session_directory / "data_files.jsonl", RecordingTimelineEntryKind::kDataFile,
                query, &local_result, error)) {
    return false;
  }

  std::stable_sort(local_result.entries.begin(), local_result.entries.end(),
                   [](const RecordingTimelineEntry& left, const RecordingTimelineEntry& right) {
                     return left.timestamp_ms < right.timestamp_ms;
                   });
  local_result.total_entries = local_result.entries.size();
  if (local_result.entries.size() > query.limit) {
    local_result.entries.resize(query.limit);
    local_result.truncated = true;
  }
  *result = std::move(local_result);
  return true;
}

const char* ToString(RecordingTimelineEntryKind kind) {
  switch (kind) {
    case RecordingTimelineEntryKind::kVehicleState:
      return "vehicle_state";
    case RecordingTimelineEntryKind::kEvent:
      return "event";
    case RecordingTimelineEntryKind::kDataFile:
      return "data_file";
  }
  return "unknown";
}

}  // namespace recording
}  // namespace cockpit
