#include "topic_list.h"

#include "topic_store.h"

#include <iostream>

namespace cockpit {
namespace topic {

int RunListCommand(const cockpit::config::Config& config, const CommandLine&) {
  const TopicStore store(config);
  const auto registry = store.LoadRegistry();
  for (const auto& item : registry) {
    std::cout << item.first << std::endl;
  }
  return 0;
}

}  // namespace topic
}  // namespace cockpit
