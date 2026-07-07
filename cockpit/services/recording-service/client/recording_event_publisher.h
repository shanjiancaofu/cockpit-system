#pragma once

#include <string>

namespace cockpit {
namespace recording {

class RecordingEventPublisher {
 public:
  explicit RecordingEventPublisher(std::string address);

  bool Publish(std::int64_t timestamp_ms, const std::string& topic,
               const std::string& payload_json) const;

 private:
  std::string address_;
};

}  // namespace recording
}  // namespace cockpit
