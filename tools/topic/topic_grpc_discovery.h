#pragma once

#include <string>
#include <vector>

namespace cockpit {
namespace topic {

struct TopicMetadata {
  std::string name;
  std::string message_type;
  std::string source;
  bool subscribable = false;
  bool publishable = false;
};

class TopicGrpcDiscovery {
 public:
  TopicGrpcDiscovery(std::string address, int timeout_ms);

  int List(std::vector<TopicMetadata>* topics) const;
  int Get(const std::string& topic, TopicMetadata* metadata) const;

 private:
  std::string address_;
  int timeout_ms_;
};

}  // namespace topic
}  // namespace cockpit
