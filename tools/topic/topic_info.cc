#include "topic_info.h"

#include "topic_store.h"

#include <filesystem>
#include <iostream>

namespace cockpit {
namespace topic {

int RunInfoCommand(const cockpit::config::Config& config, const CommandLine& line) {
  if (line.positionals.empty()) {
    std::cerr << "topic info requires <topic>\n";
    return 2;
  }

  const std::string topic = line.positionals[0];
  const TopicStore store(config);
  const auto path = store.TopicFile(topic);
  std::cout << "topic: " << topic << '\n';
  std::cout << "file: " << path.string() << '\n';
  std::cout << "messages: " << (std::filesystem::exists(path) ? store.CountLines(path) : 0)
            << '\n';
  if (std::filesystem::exists(path)) {
    std::cout << "bytes: " << std::filesystem::file_size(path) << '\n';
  } else {
    std::cout << "bytes: 0\n";
  }
  return 0;
}

}  // namespace topic
}  // namespace cockpit
