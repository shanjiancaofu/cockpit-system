#include "cockpit/modules/recording/recording_data_file.h"

#include <sstream>

namespace cockpit {
namespace recording {
namespace {

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

}  // namespace

bool RecordingDataFile::IsValid() const {
  return timestamp_ms > 0 && !source.empty() && !kind.empty() && !path.empty();
}

std::string RecordingDataFile::ToJson() const {
  std::ostringstream output;
  output << "{"
         << "\"timestamp_ms\":" << timestamp_ms << ","
         << "\"source\":\"" << EscapeJson(source) << "\","
         << "\"kind\":\"" << EscapeJson(kind) << "\","
         << "\"path\":\"" << EscapeJson(path) << "\","
         << "\"size_bytes\":" << size_bytes << ","
         << "\"checksum\":\"" << EscapeJson(checksum) << "\""
         << "}";
  return output.str();
}

}  // namespace recording
}  // namespace cockpit
