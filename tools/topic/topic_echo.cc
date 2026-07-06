#include "topic_echo.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "topic_grpc_subscriber.h"
#include "topic_store.h"

#include "cockpit/core/config/system_config.h"

namespace cockpit {
namespace topic {

int RunEchoCommand(const cockpit::config::SystemConfig& config, const CommandLine& line) {
  if (line.positionals.empty()) {
    std::cerr << "topic echo requires <topic>\n";
    return 2;
  }

  const std::string topic = line.positionals[0];
  const std::string backend = Option(line, "backend", config.tools().topic.backend);
  if (backend == "grpc") {
    const int count = HasFlag(line, "follow")
                          ? 0
                          : std::max(1, OptionInt(line, "count", OptionInt(line, "tail", 10)));
    const int max_hz = std::max(1, OptionInt(line, "max-hz", 100));
    const auto& gateway = config.services().gateway;
    const int timeout_ms = std::max(
        1, OptionInt(line, "timeout-ms", std::max(gateway.stream_timeout_ms, count * 1000)));
    const TopicGrpcSubscriber subscriber(gateway.grpc.listen_address, timeout_ms);
    return subscriber.Stream(topic, count, max_hz, [](const TopicSample& sample) {
      std::cout << sample.json << std::endl;
    });
  }
  if (backend != "file") {
    std::cerr << "unsupported topic backend: " << backend << '\n';
    return 2;
  }

  const TopicStore store(config);
  const auto path = store.TopicFile(topic);
  const int tail = HasFlag(line, "all") ? 0 : std::max(1, OptionInt(line, "tail", 10));
  for (const auto& message : store.ReadLastLines(path, tail)) {
    std::cout << message << std::endl;
  }

  if (!HasFlag(line, "follow")) {
    return 0;
  }

  std::uintmax_t offset = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
  while (true) {
    if (std::filesystem::exists(path) && std::filesystem::file_size(path) > offset) {
      std::ifstream input(path);
      input.seekg(static_cast<std::streamoff>(offset));
      std::string line_value;
      while (std::getline(input, line_value)) {
        std::cout << line_value << std::endl;
      }
      offset = std::filesystem::file_size(path);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

}  // namespace topic
}  // namespace cockpit
