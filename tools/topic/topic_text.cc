#include "topic_text.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace cockpit {
namespace topic {

std::string Trim(const std::string& value) {
  const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                     return std::isspace(ch) != 0;
                   }).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string EscapeJson(const std::string& value) {
  std::ostringstream out;
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << ch;
        break;
    }
  }
  return out.str();
}

bool LooksLikeJsonValue(const std::string& payload) {
  const std::string value = Trim(payload);
  if (value.empty()) {
    return false;
  }
  const char first = value.front();
  return first == '{' || first == '[' || first == '"' || first == '-' ||
         std::isdigit(static_cast<unsigned char>(first)) != 0 || value == "true" ||
         value == "false" || value == "null";
}

}  // namespace topic
}  // namespace cockpit
