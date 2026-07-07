#include "cockpit/modules/recording/recording_event.h"

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

bool RecordingEvent::IsValid() const {
  return timestamp_ms > 0 && !topic.empty() && !payload_json.empty();
}

std::string RecordingEvent::ToJson() const {
  std::ostringstream output;
  output << "{"
         << "\"timestamp_ms\":" << timestamp_ms << ',' << "\"topic\":\"" << EscapeJson(topic)
         << "\","
         << "\"payload\":" << payload_json << "}";
  return output.str();
}

}  // namespace recording
}  // namespace cockpit
