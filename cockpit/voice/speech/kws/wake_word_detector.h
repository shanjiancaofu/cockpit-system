#pragma once

#include <string>

#include "cockpit/driver/audio/frames/audio_frame.h"

namespace cockpit {
namespace agent {

struct WakeWordResult {
  bool detected = false;
  std::string keyword;
  std::string error;
};

class WakeWordDetector {
 public:
  virtual ~WakeWordDetector() = default;

  virtual WakeWordResult Analyze(const audio::AudioFrame& frame) = 0;
  virtual void Reset() = 0;
};

}  // namespace agent
}  // namespace cockpit
