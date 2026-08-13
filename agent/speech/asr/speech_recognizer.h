#pragma once

#include <chrono>
#include <string>

#include "agent/speech/segment/speech_segment.h"

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

  virtual SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment,
                                            std::chrono::steady_clock::time_point deadline) = 0;
  virtual void Cancel() = 0;
};

}  // namespace voice
}  // namespace cockpit
