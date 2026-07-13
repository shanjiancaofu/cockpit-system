#include "topic_list.h"

#include <algorithm>
#include <iostream>

#include "topic_grpc_discovery.h"
#include "topic_store.h"

#include "cockpit/core/config/system_config.h"

namespace cockpit {
namespace topic {

int RunListCommand(const cockpit::config::SystemConfig& config, const CommandLine& line) {
  const std::string backend = Option(line, "backend", config.tools().topic.backend);
  if (backend == "grpc") {
    const auto& gateway = config.services().gateway;
    const int timeout_ms = std::max(1, OptionInt(line, "timeout-ms", gateway.stream_timeout_ms));
    const TopicGrpcDiscovery discovery(gateway.grpc.listen_address, timeout_ms);
    std::vector<proto::gateway::TopicMetadata> topics;
    const int result = discovery.List(&topics);
    if (result != 0) {
      return result;
    }
    for (const auto& topic : topics) {
      std::cout << topic.name() << std::endl;
    }
    return 0;
  }
  if (backend != "file") {
    std::cerr << "unsupported topic backend: " << backend << '\n';
    return 2;
  }

  const TopicStore store(config);
  const auto registry = store.LoadRegistry();
  for (const auto& item : registry) {
    std::cout << item.first << std::endl;
  }
  return 0;
}

}  // namespace topic
}  // namespace cockpit
