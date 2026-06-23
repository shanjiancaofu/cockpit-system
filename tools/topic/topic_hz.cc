#include "topic_hz.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "topic_grpc_subscriber.h"
#include "topic_message.h"
#include "topic_store.h"

#include "core/config/system_config.h"

namespace cockpit {
namespace topic {
namespace {

void PushTimestamp(std::vector<std::int64_t>* timestamps, std::int64_t timestamp,
                   std::size_t window) {
  timestamps->push_back(timestamp);
  while (timestamps->size() > window) {
    timestamps->erase(timestamps->begin());
  }
}

bool PrintHzStats(const std::vector<std::int64_t>& timestamps) {
  if (timestamps.size() < 2) {
    std::cout << "not enough messages to calculate rate\n";
    return false;
  }

  const auto first = timestamps.front();
  const auto last = timestamps.back();
  const auto duration_ms = last - first;
  if (duration_ms <= 0) {
    std::cout << "not enough timestamp span to calculate rate\n";
    return false;
  }

  std::int64_t min_delta = std::numeric_limits<std::int64_t>::max();
  std::int64_t max_delta = 0;
  for (std::size_t i = 1; i < timestamps.size(); ++i) {
    const auto delta = timestamps[i] - timestamps[i - 1];
    if (delta <= 0) {
      continue;
    }
    min_delta = std::min(min_delta, delta);
    max_delta = std::max(max_delta, delta);
  }

  const double samples = static_cast<double>(timestamps.size() - 1);
  const double rate = samples * 1000.0 / static_cast<double>(duration_ms);
  std::cout << std::fixed << std::setprecision(3);
  std::cout << "average rate: " << rate << " Hz\n";
  std::cout << "  window: " << timestamps.size() << " messages\n";
  std::cout << "  span: " << static_cast<double>(duration_ms) / 1000.0 << " s\n";
  if (min_delta != std::numeric_limits<std::int64_t>::max()) {
    std::cout << "  min delta: " << static_cast<double>(min_delta) / 1000.0 << " s\n";
    std::cout << "  max delta: " << static_cast<double>(max_delta) / 1000.0 << " s\n";
  }
  return true;
}

}  // namespace

int RunHzCommand(const cockpit::config::SystemConfig& config, const CommandLine& line) {
  if (line.positionals.empty()) {
    std::cerr << "topic hz requires <topic>\n";
    return 2;
  }

  const std::string topic = line.positionals[0];
  const auto window = static_cast<std::size_t>(std::max(2, OptionInt(line, "window", 100)));
  const std::string backend = Option(line, "backend", config.tools().topic.backend);
  if (backend == "grpc") {
    std::vector<std::int64_t> timestamps;
    auto last_report = std::chrono::steady_clock::now();
    const int count = HasFlag(line, "follow")
                          ? 0
                          : std::max(2, OptionInt(line, "count", static_cast<int>(window)));
    const int max_hz = std::max(1, OptionInt(line, "max-hz", 100));
    const auto& gateway = config.services().gateway;
    const int timeout_ms = std::max(
        1, OptionInt(line, "timeout-ms", std::max(gateway.stream_timeout_ms, count * 1000)));
    const TopicGrpcSubscriber subscriber(gateway.grpc.listen_address, timeout_ms);
    const int result = subscriber.Stream(topic, count, max_hz, [&](const TopicSample& sample) {
      PushTimestamp(&timestamps, sample.timestamp_ms, window);
      const auto now = std::chrono::steady_clock::now();
      if (HasFlag(line, "follow") && now - last_report >= std::chrono::seconds(1)) {
        PrintHzStats(timestamps);
        last_report = now;
      }
    });
    if (!HasFlag(line, "follow")) {
      PrintHzStats(timestamps);
    }
    return result;
  }
  if (backend != "file") {
    std::cerr << "unsupported topic backend: " << backend << '\n';
    return 2;
  }

  const TopicStore store(config);
  const auto path = store.TopicFile(topic);
  auto timestamps = ExtractTimestamps(store.ReadLastLines(path, static_cast<int>(window)));
  while (timestamps.size() > window) {
    timestamps.erase(timestamps.begin());
  }
  PrintHzStats(timestamps);

  if (!HasFlag(line, "follow")) {
    return timestamps.size() >= 2 ? 0 : 1;
  }

  std::uintmax_t offset = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
  auto last_report = std::chrono::steady_clock::now();
  while (true) {
    bool has_new_message = false;
    if (std::filesystem::exists(path) && std::filesystem::file_size(path) > offset) {
      std::ifstream input(path);
      input.seekg(static_cast<std::streamoff>(offset));
      std::string line_value;
      while (std::getline(input, line_value)) {
        const auto timestamp = ExtractTimestampMs(line_value);
        if (timestamp.has_value()) {
          PushTimestamp(&timestamps, *timestamp, window);
          has_new_message = true;
        }
      }
      offset = std::filesystem::file_size(path);
    }

    const auto now = std::chrono::steady_clock::now();
    if (has_new_message && now - last_report >= std::chrono::seconds(1)) {
      PrintHzStats(timestamps);
      last_report = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

}  // namespace topic
}  // namespace cockpit
