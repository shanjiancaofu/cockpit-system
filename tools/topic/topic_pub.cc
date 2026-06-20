#include "topic_pub.h"

#include "topic_store.h"
#include "topic_text.h"
#include "core/utils/Time.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace cockpit {
namespace topic {

int RunPubCommand(const cockpit::config::SystemConfig& config, const CommandLine& line) {
  if (line.positionals.empty()) {
    std::cerr << "topic pub requires <topic> and <payload>\n";
    return 2;
  }

  const std::string topic = line.positionals[0];
  std::string payload = Option(line, "payload", "");
  if (payload.empty()) {
    payload = Join(line.positionals, 1);
  }
  if (payload.empty()) {
    std::cerr << "topic pub requires a payload\n";
    return 2;
  }

  const int repeat = std::max(1, OptionInt(line, "repeat", 1));
  const int rate_ms = std::max(0, OptionInt(line, "rate-ms", 1000));
  const TopicStore store(config);
  std::filesystem::create_directories(store.topic_dir());
  const auto path = store.TopicFile(topic);

  for (int i = 0; i < repeat; ++i) {
    const auto timestamp = cockpit::utils::NowMs();
    std::ofstream output(path, std::ios::app);
    output << "{\"timestamp_ms\":" << timestamp << ",\"topic\":\"" << EscapeJson(topic)
           << "\",\"payload\":";
    if (LooksLikeJsonValue(payload)) {
      output << payload;
    } else {
      output << '"' << EscapeJson(payload) << '"';
    }
    output << "}\n";
    output.close();

    auto registry = store.LoadRegistry();
    registry[topic] = {timestamp, path.filename().string()};
    store.SaveRegistry(registry);

    if (i + 1 < repeat && rate_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(rate_ms));
    }
  }
  return 0;
}

}  // namespace topic
}  // namespace cockpit
