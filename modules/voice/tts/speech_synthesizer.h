#pragma once

#include <string>

#include "modules/audio/wav/wav_file.h"

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

  virtual SpeechSynthesisResult Synthesize(const std::string& text) = 0;
};

}  // namespace voice
}  // namespace cockpit
