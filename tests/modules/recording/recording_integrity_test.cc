#include "cockpit/modules/recording/recording_integrity.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool HasIssue(const cockpit::recording::RecordingIntegrityResult& result,
              cockpit::recording::RecordingIntegrityIssueKind kind) {
  for (const auto& issue : result.issues) {
    if (issue.kind == kind) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("cockpit_recording_integrity_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory / "artifacts");
  std::ofstream(directory / "artifacts/valid.bin", std::ios::binary) << "hello";
  std::ofstream(directory / "artifacts/size.bin", std::ios::binary) << "hello";
  std::ofstream(directory / "artifacts/checksum.bin", std::ios::binary) << "hello";
  std::filesystem::create_directory(directory / "artifacts/not-a-file");
  std::ofstream index(directory / "data_files.jsonl");
  index << "{\"source\":\"camera\",\"path\":\"artifacts/valid.bin\",\"size_bytes\":5,"
           "\"checksum\":\"fnv1a64:a430d84680aabd0b\",\"copied_into_session\":true}\n"
        << "{\"source\":\"camera\",\"path\":\"artifacts/missing.bin\",\"size_bytes\":1,"
           "\"checksum\":\"\",\"copied_into_session\":true}\n"
        << "{\"source\":\"audio\",\"path\":\"artifacts/size.bin\",\"size_bytes\":4,"
           "\"checksum\":\"\",\"copied_into_session\":true}\n"
        << "{\"source\":\"audio\",\"path\":\"artifacts/checksum.bin\",\"size_bytes\":5,"
           "\"checksum\":\"fnv1a64:0000000000000000\",\"copied_into_session\":true}\n"
        << "{\"source\":\"camera\",\"path\":\"artifacts/not-a-file\",\"size_bytes\":0,"
           "\"checksum\":\"\",\"copied_into_session\":true}\n"
        << "{\"source\":\"camera\",\"path\":\"../outside.bin\",\"size_bytes\":0,"
           "\"checksum\":\"\",\"copied_into_session\":true}\n"
        << "{bad}\n";
  index.close();

  cockpit::recording::RecordingIntegrityResult result;
  std::string error;
  const bool verified =
      cockpit::recording::RecordingIntegrityVerifier::Verify(directory, &result, &error);
  const bool ok =
      Check(verified, "recording integrity verification failed") &&
      Check(!result.healthy, "invalid recording was reported healthy") &&
      Check(result.index_entries == 6, "recording integrity index count mismatch") &&
      Check(result.files_checked == 3, "recording integrity checked file count mismatch") &&
      Check(result.checksums_checked == 2, "recording integrity checksum count mismatch") &&
      Check(result.issues.size() == 6, "recording integrity issue count mismatch") &&
      Check(HasIssue(result, cockpit::recording::RecordingIntegrityIssueKind::kInvalidIndex),
            "invalid index issue missing") &&
      Check(HasIssue(result, cockpit::recording::RecordingIntegrityIssueKind::kUnsafePath),
            "unsafe path issue missing") &&
      Check(HasIssue(result, cockpit::recording::RecordingIntegrityIssueKind::kMissingFile),
            "missing file issue missing") &&
      Check(HasIssue(result, cockpit::recording::RecordingIntegrityIssueKind::kNotRegularFile),
            "non-regular file issue missing") &&
      Check(HasIssue(result, cockpit::recording::RecordingIntegrityIssueKind::kSizeMismatch),
            "size mismatch issue missing") &&
      Check(HasIssue(result, cockpit::recording::RecordingIntegrityIssueKind::kChecksumMismatch),
            "checksum mismatch issue missing");
  if (!verified) {
    std::cerr << error << '\n';
  }
  std::filesystem::remove_all(directory);
  return ok ? 0 : 1;
}
