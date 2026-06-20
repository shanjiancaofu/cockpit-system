#include "topic_echo.h"

#include "topic_store.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace cockpit {
namespace topic {

int RunEchoCommand(const cockpit::config::SystemConfig& config, const CommandLine& line) {
  if (line.positionals.empty()) {
    std::cerr << "topic echo requires <topic>\n";
    return 2;
  }

  const std::string topic = line.positionals[0];
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
