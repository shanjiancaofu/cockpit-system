#pragma once

#include <string>
#include <vector>

#include "gateway.pb.h"

namespace cockpit {
namespace topic {

class TopicGrpcDiscovery {
 public:
  TopicGrpcDiscovery(std::string address, int timeout_ms);

  int List(std::vector<proto::gateway::TopicMetadata>* topics) const;
  int Get(const std::string& topic, proto::gateway::TopicMetadata* metadata) const;

 private:
  std::string address_;
  int timeout_ms_;
};

}  // namespace topic
}  // namespace cockpit
