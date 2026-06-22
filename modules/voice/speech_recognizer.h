#pragma once

#include "modules/audio/speech_segment.h"

#include <string>

namespace cockpit {
namespace voice {

struct SpeechRecognitionResult {
  bool success = false;
  std::string text;
  std::string provider;
  float confidence = 0.0F;
  std::string error;
};

class SpeechRecognizer {
 public:
  virtual ~SpeechRecognizer() = default;

  virtual SpeechRecognitionResult Recognize(
      const audio::SpeechSegment& segment) = 0;
};

}  // namespace voice
}  // namespace cockpit
