#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace cockpit {
namespace topic {

struct TopicSample {
  std::int64_t timestamp_ms = 0;
  std::string json;
};

class TopicGrpcSubscriber {
 public:
  using SampleHandler = std::function<void(const TopicSample&)>;

  TopicGrpcSubscriber(std::string address, int timeout_ms);

  int Stream(const std::string& topic, int count, int max_hz,
             const SampleHandler& handler) const;

 private:
  std::string address_;
  int timeout_ms_;
};

}  // namespace topic
}  // namespace cockpit
