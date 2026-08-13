#pragma once

#include <chrono>
#include <string>

#include "cockpit/modules/audio/wav/wav_file.h"

namespace cockpit {
namespace voice {

struct SpeechSynthesisResult {
  bool success = false;
  audio::PcmBuffer audio;
  std::string provider;
  std::string error;
};

class SpeechSynthesizer {
 public:
  virtual ~SpeechSynthesizer() = default;

  virtual SpeechSynthesisResult Synthesize(const std::string& text,
                                           std::chrono::steady_clock::time_point deadline) = 0;
  virtual void Cancel() = 0;
};

}  // namespace voice
}  // namespace cockpit
