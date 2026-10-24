#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace recording {

struct RecordingEvent {
  std::int64_t timestamp_ms = 0;
  std::string topic;
  std::string payload_json;

  bool IsValid() const;
  std::string ToJson() const;
};

}  // namespace recording
}  // namespace cockpit
