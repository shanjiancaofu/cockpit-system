#pragma once

#include <string>
#include <string_view>

namespace cockpit {
namespace voice {

class TranscriptNormalizer {
 public:
  static std::string Normalize(std::string_view transcript);
};

}  // namespace voice
}  // namespace cockpit
