#include "cockpit/modules/recording/recording_data_file.h"

#include <sstream>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace recording {

bool RecordingDataFile::IsValid() const {
  return timestamp_ms > 0 && !source.empty() && !kind.empty() && !path.empty();
}

std::string RecordingDataFile::ToJson() const {
  std::ostringstream output;
  output << "{"
         << "\"timestamp_ms\":" << timestamp_ms << ","
         << "\"source\":\"" << json::EscapeString(source) << "\","
         << "\"kind\":\"" << json::EscapeString(kind) << "\","
         << "\"path\":\"" << json::EscapeString(path) << "\","
         << "\"size_bytes\":" << size_bytes << ","
         << "\"checksum\":\"" << json::EscapeString(checksum) << "\","
         << "\"copied_into_session\":" << (copy_into_session ? "true" : "false") << "}";
  return output.str();
}

}  // namespace recording
}  // namespace cockpit
