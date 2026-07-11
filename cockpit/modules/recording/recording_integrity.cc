#include "cockpit/modules/recording/recording_integrity.h"

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <exception>
#include <fstream>
#include <limits>
#include <utility>

#include "cockpit/core/json/json.h"
#include "cockpit/modules/recording/file_checksum.h"

namespace cockpit {
namespace recording {
namespace {

constexpr std::size_t kMaximumLineBytes = std::size_t{1024} * 1024U;
constexpr char kFnvPrefix[] = "fnv1a64:";

struct IndexedFile {
  std::string source;
  std::string path;
  std::uint64_t size_bytes = 0;
  std::string checksum;
  bool copied_into_session = false;
};

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

void AddIssue(RecordingIntegrityIssue issue, RecordingIntegrityResult* result) {
  result->healthy = false;
  result->issues.push_back(std::move(issue));
}

RecordingIntegrityIssue MakeIssue(RecordingIntegrityIssueKind kind, std::uint64_t line_number,
                                  const std::string& source, const std::string& path,
                                  const std::string& message) {
  RecordingIntegrityIssue issue;
  issue.kind = kind;
  issue.line_number = line_number;
  issue.source = source;
  issue.path = path;
  issue.message = message;
  return issue;
}

bool ParseIndexLine(const std::string& line, IndexedFile* file) {
  if (line.size() > kMaximumLineBytes || !json::IsValidValue(line)) {
    return false;
  }
  try {
    const YAML::Node node = YAML::Load(line);
    if (!node.IsMap()) {
      return false;
    }
    file->source = node["source"].as<std::string>();
    file->path = node["path"].as<std::string>();
    file->size_bytes = node["size_bytes"].as<std::uint64_t>();
    file->checksum = node["checksum"].as<std::string>("");
    file->copied_into_session = node["copied_into_session"].as<bool>(false);
    return !file->source.empty() && !file->path.empty();
  } catch (const std::exception&) {
    return false;
  }
}

bool IsWithinDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
  auto path_part = path.begin();
  auto directory_part = directory.begin();
  for (; directory_part != directory.end(); ++directory_part, ++path_part) {
    if (path_part == path.end() || *path_part != *directory_part) {
      return false;
    }
  }
  return true;
}

std::filesystem::path ResolvePath(const IndexedFile& file,
                                  const std::filesystem::path& session_directory, bool* safe) {
  const std::filesystem::path indexed_path(file.path);
  if (!file.copied_into_session) {
    *safe = true;
    return indexed_path;
  }
  if (indexed_path.is_absolute()) {
    *safe = false;
    return {};
  }
  const std::filesystem::path normalized_session =
      std::filesystem::absolute(session_directory).lexically_normal();
  const std::filesystem::path resolved = (normalized_session / indexed_path).lexically_normal();
  *safe = IsWithinDirectory(resolved, normalized_session) && resolved != normalized_session;
  return resolved;
}

void VerifyFile(const IndexedFile& file, std::uint64_t line_number,
                const std::filesystem::path& session_directory, RecordingIntegrityResult* result) {
  bool safe = false;
  const std::filesystem::path path = ResolvePath(file, session_directory, &safe);
  if (!safe) {
    AddIssue(MakeIssue(RecordingIntegrityIssueKind::kUnsafePath, line_number, file.source,
                       file.path, "copied artifact path escapes the recording session"),
             result);
    return;
  }

  std::error_code filesystem_error;
  const bool exists = std::filesystem::exists(path, filesystem_error);
  if (filesystem_error || !exists) {
    AddIssue(
        MakeIssue(RecordingIntegrityIssueKind::kMissingFile, line_number, file.source, file.path,
                  filesystem_error ? "inspect indexed file failed: " + filesystem_error.message()
                                   : "indexed file does not exist"),
        result);
    return;
  }
  if (file.copied_into_session) {
    const std::filesystem::path canonical_session =
        std::filesystem::canonical(session_directory, filesystem_error);
    const std::filesystem::path canonical_path =
        filesystem_error ? std::filesystem::path()
                         : std::filesystem::canonical(path, filesystem_error);
    if (filesystem_error || !IsWithinDirectory(canonical_path, canonical_session) ||
        canonical_path == canonical_session) {
      AddIssue(MakeIssue(RecordingIntegrityIssueKind::kUnsafePath, line_number, file.source,
                         file.path, "copied artifact resolves outside the recording session"),
               result);
      return;
    }
  }
  if (!std::filesystem::is_regular_file(path, filesystem_error) || filesystem_error) {
    AddIssue(MakeIssue(RecordingIntegrityIssueKind::kNotRegularFile, line_number, file.source,
                       file.path, "indexed path is not a regular file"),
             result);
    return;
  }

  const std::uintmax_t size = std::filesystem::file_size(path, filesystem_error);
  if (filesystem_error || size > std::numeric_limits<std::uint64_t>::max()) {
    AddIssue(MakeIssue(RecordingIntegrityIssueKind::kNotRegularFile, line_number, file.source,
                       file.path, "read indexed file size failed"),
             result);
    return;
  }
  ++result->files_checked;
  const std::uint64_t actual_size = static_cast<std::uint64_t>(size);
  if (actual_size != file.size_bytes) {
    RecordingIntegrityIssue issue;
    issue.kind = RecordingIntegrityIssueKind::kSizeMismatch;
    issue.line_number = line_number;
    issue.source = file.source;
    issue.path = file.path;
    issue.message = "indexed file size does not match";
    issue.expected_size_bytes = file.size_bytes;
    issue.actual_size_bytes = actual_size;
    AddIssue(std::move(issue), result);
  }

  if (file.checksum.empty()) {
    ++result->checksums_unavailable;
    return;
  }
  if (file.checksum.rfind(kFnvPrefix, 0) != 0) {
    ++result->checksums_unavailable;
    return;
  }
  std::string actual_checksum;
  std::string checksum_error;
  if (!ComputeFnv1a64(path, &actual_checksum, &checksum_error)) {
    RecordingIntegrityIssue issue;
    issue.kind = RecordingIntegrityIssueKind::kChecksumMismatch;
    issue.line_number = line_number;
    issue.source = file.source;
    issue.path = file.path;
    issue.message = checksum_error;
    issue.expected_checksum = file.checksum;
    AddIssue(std::move(issue), result);
    return;
  }
  ++result->checksums_checked;
  if (actual_checksum != file.checksum) {
    RecordingIntegrityIssue issue;
    issue.kind = RecordingIntegrityIssueKind::kChecksumMismatch;
    issue.line_number = line_number;
    issue.source = file.source;
    issue.path = file.path;
    issue.message = "indexed file checksum does not match";
    issue.expected_checksum = file.checksum;
    issue.actual_checksum = actual_checksum;
    AddIssue(std::move(issue), result);
  }
}

}  // namespace

bool RecordingIntegrityVerifier::Verify(const std::filesystem::path& session_directory,
                                        RecordingIntegrityResult* result, std::string* error) {
  if (result == nullptr) {
    AssignError(error, "recording integrity result must not be null");
    return false;
  }
  RecordingIntegrityResult local_result;
  const std::filesystem::path index_path = session_directory / "data_files.jsonl";
  std::ifstream input(index_path);
  if (!input.is_open()) {
    AssignError(error, "open recording data file index failed: " + index_path.string());
    return false;
  }

  std::string line;
  std::uint64_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    IndexedFile file;
    if (!ParseIndexLine(line, &file)) {
      AddIssue(MakeIssue(RecordingIntegrityIssueKind::kInvalidIndex, line_number, {}, {},
                         "data file index line is invalid"),
               &local_result);
      continue;
    }
    ++local_result.index_entries;
    VerifyFile(file, line_number, session_directory, &local_result);
  }
  if (input.bad()) {
    AssignError(error, "read recording data file index failed: " + index_path.string());
    return false;
  }
  *result = std::move(local_result);
  return true;
}

const char* ToString(RecordingIntegrityIssueKind kind) {
  switch (kind) {
    case RecordingIntegrityIssueKind::kInvalidIndex:
      return "invalid_index";
    case RecordingIntegrityIssueKind::kUnsafePath:
      return "unsafe_path";
    case RecordingIntegrityIssueKind::kMissingFile:
      return "missing_file";
    case RecordingIntegrityIssueKind::kNotRegularFile:
      return "not_regular_file";
    case RecordingIntegrityIssueKind::kSizeMismatch:
      return "size_mismatch";
    case RecordingIntegrityIssueKind::kChecksumMismatch:
      return "checksum_mismatch";
  }
  return "unknown";
}

}  // namespace recording
}  // namespace cockpit
