#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cockpit {
namespace recording {

enum class RecordingIntegrityIssueKind {
  kInvalidIndex,
  kUnsafePath,
  kMissingFile,
  kNotRegularFile,
  kSizeMismatch,
  kChecksumMismatch,
};

struct RecordingIntegrityIssue {
  RecordingIntegrityIssueKind kind = RecordingIntegrityIssueKind::kInvalidIndex;
  std::uint64_t line_number = 0;
  std::string source;
  std::string path;
  std::string message;
  std::uint64_t expected_size_bytes = 0;
  std::uint64_t actual_size_bytes = 0;
  std::string expected_checksum;
  std::string actual_checksum;
};

struct RecordingIntegrityResult {
  bool healthy = true;
  std::uint64_t index_entries = 0;
  std::uint64_t files_checked = 0;
  std::uint64_t checksums_checked = 0;
  std::uint64_t checksums_unavailable = 0;
  std::vector<RecordingIntegrityIssue> issues;
};

class RecordingIntegrityVerifier {
 public:
  static bool Verify(const std::filesystem::path& session_directory,
                     RecordingIntegrityResult* result, std::string* error);
};

const char* ToString(RecordingIntegrityIssueKind kind);

}  // namespace recording
}  // namespace cockpit
