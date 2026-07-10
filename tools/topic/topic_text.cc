#include "topic_text.h"

#include <algorithm>
#include <cctype>

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

}  // namespace topic
}  // namespace cockpit
