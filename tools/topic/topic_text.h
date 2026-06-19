#pragma once

#include <string>

namespace cockpit {
namespace topic {

std::string Trim(const std::string& value);
std::string EscapeJson(const std::string& value);
bool LooksLikeJsonValue(const std::string& payload);

}  // namespace topic
}  // namespace cockpit
