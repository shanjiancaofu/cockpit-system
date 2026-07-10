#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace recording {

struct RecordingDataFile {
  std::int64_t timestamp_ms = 0;
  std::string source;
  std::string kind;
  std::string path;
  std::uint64_t size_bytes = 0;
  std::string checksum;

  bool IsValid() const;
  std::string ToJson() const;
};

}  // namespace recording
}  // namespace cockpit
