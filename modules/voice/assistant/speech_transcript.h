#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace voice {

struct SpeechTranscript {
  std::uint64_t id = 0;
  std::uint64_t timestamp_ms = 0;
  std::uint64_t start_sequence = 0;
  std::uint64_t end_sequence = 0;
  std::uint64_t duration_ms = 0;
  bool truncated = false;
  bool discontinuous = false;
  std::string text;
  std::string provider;
  float confidence = 0.0F;
};

}  // namespace voice
}  // namespace cockpit
