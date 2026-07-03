#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace voice {

struct VoiceOutputMetrics {
  std::uint64_t queued = 0;
  std::uint64_t played = 0;
  std::uint64_t failed = 0;
  std::uint64_t dropped = 0;
  std::uint64_t reconnects = 0;
  std::uint64_t consecutive_failures = 0;
  std::uint64_t last_success_timestamp_ms = 0;
  bool available = false;
};

class VoiceResponseSink {
 public:
  virtual ~VoiceResponseSink() = default;

  virtual bool Submit(std::string text) = 0;
  virtual VoiceOutputMetrics metrics() const = 0;
  virtual void Stop() {
  }
};

}  // namespace voice
}  // namespace cockpit
