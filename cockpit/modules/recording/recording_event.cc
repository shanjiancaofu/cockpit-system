#include "cockpit/modules/recording/recording_event.h"

#include <sstream>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace recording {

bool RecordingEvent::IsValid() const {
  return timestamp_ms > 0 && !topic.empty() && !payload_json.empty() &&
         json::IsValidValue(payload_json);
}

std::string RecordingEvent::ToJson() const {
  std::ostringstream output;
  output << "{"
         << "\"timestamp_ms\":" << timestamp_ms << ',' << "\"topic\":\""
         << json::EscapeString(topic) << "\","
         << "\"payload\":" << payload_json << "}";
  return output.str();
}

}  // namespace recording
}  // namespace cockpit
