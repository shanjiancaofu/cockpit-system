#include "topic_message.h"

#include <cctype>

namespace cockpit {
namespace topic {

std::optional<std::int64_t> ExtractTimestampMs(const std::string& message) {
  const std::string key = "\"timestamp_ms\":";
  const auto key_pos = message.find(key);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }

  std::size_t begin = key_pos + key.size();
  while (begin < message.size() && std::isspace(static_cast<unsigned char>(message[begin])) != 0) {
    ++begin;
  }

  std::size_t end = begin;
  while (end < message.size() &&
         (std::isdigit(static_cast<unsigned char>(message[end])) != 0 || message[end] == '-')) {
    ++end;
  }
  if (begin == end) {
    return std::nullopt;
  }

  try {
    return std::stoll(message.substr(begin, end - begin));
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<std::int64_t> ExtractTimestamps(const std::vector<std::string>& messages) {
  std::vector<std::int64_t> timestamps;
  for (const auto& message : messages) {
    const auto timestamp = ExtractTimestampMs(message);
    if (timestamp.has_value()) {
      timestamps.push_back(*timestamp);
    }
  }
  return timestamps;
}

}  // namespace topic
}  // namespace cockpit
