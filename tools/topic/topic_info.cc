#include "topic_info.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

#include "topic_grpc_discovery.h"
#include "topic_store.h"

#include "core/config/system_config.h"

namespace cockpit {
namespace topic {

int RunInfoCommand(const cockpit::config::SystemConfig& config, const CommandLine& line) {
  if (line.positionals.empty()) {
    std::cerr << "topic info requires <topic>\n";
    return 2;
  }

  const std::string topic = line.positionals[0];
  const std::string backend = Option(line, "backend", config.tools().topic.backend);
  if (backend == "grpc") {
    const auto& gateway = config.services().gateway;
    const int timeout_ms = std::max(1, OptionInt(line, "timeout-ms", gateway.stream_timeout_ms));
    const TopicGrpcDiscovery discovery(gateway.grpc.listen_address, timeout_ms);
    TopicMetadata metadata;
    const int result = discovery.Get(topic, &metadata);
    if (result != 0) {
      return result;
    }
    std::cout << "topic: " << metadata.name << '\n';
    std::cout << "type: " << metadata.message_type << '\n';
    std::cout << "source: " << metadata.source << '\n';
    std::cout << "transport: grpc\n";
    std::cout << "subscribable: " << (metadata.subscribable ? "true" : "false") << '\n';
    std::cout << "publishable: " << (metadata.publishable ? "true" : "false") << '\n';
    return 0;
  }
  if (backend != "file") {
    std::cerr << "unsupported topic backend: " << backend << '\n';
    return 2;
  }

  const TopicStore store(config);
  const auto path = store.TopicFile(topic);
  std::cout << "topic: " << topic << '\n';
  std::cout << "file: " << path.string() << '\n';
  std::cout << "messages: " << (std::filesystem::exists(path) ? store.CountLines(path) : 0) << '\n';
  if (std::filesystem::exists(path)) {
    std::cout << "bytes: " << std::filesystem::file_size(path) << '\n';
  } else {
    std::cout << "bytes: 0\n";
  }
  return 0;
}

}  // namespace topic
}  // namespace cockpit
