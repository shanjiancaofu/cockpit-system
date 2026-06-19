#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cockpit {
namespace topic {

std::optional<std::int64_t> ExtractTimestampMs(const std::string& message);
std::vector<std::int64_t> ExtractTimestamps(const std::vector<std::string>& messages);

}  // namespace topic
}  // namespace cockpit
