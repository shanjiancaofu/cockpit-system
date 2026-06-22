#include "modules/voice/mock_speech_recognizer.h"

#include <string>

namespace cockpit {
namespace voice {

SpeechRecognitionResult MockSpeechRecognizer::Recognize(
    const audio::SpeechSegment& segment) {
  if (segment.samples.empty()) {
    return {false, {}, "mock", 0.0F, "speech segment is empty"};
  }
  SpeechRecognitionResult result;
  result.success = true;
  result.text = "mock transcript duration_ms=" +
                std::to_string(segment.DurationMs());
  result.provider = "mock";
  result.confidence = 1.0F;
  return result;
}

}  // namespace voice
}  // namespace cockpit
