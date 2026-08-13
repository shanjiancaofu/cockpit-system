#include "agent/speech/asr/mock_speech_recognizer.h"

#include <string>

namespace cockpit {
namespace voice {

SpeechRecognitionResult MockSpeechRecognizer::Recognize(
    const audio::SpeechSegment& segment, std::chrono::steady_clock::time_point deadline) {
  if (std::chrono::steady_clock::now() >= deadline) {
    return {false, {}, "mock", 0.0F, "speech recognition deadline exceeded"};
  }
  if (segment.samples.empty()) {
    return {false, {}, "mock", 0.0F, "speech segment is empty"};
  }
  SpeechRecognitionResult result;
  result.success = true;
  result.text = "mock transcript duration_ms=" + std::to_string(segment.DurationMs());
  result.provider = "mock";
  result.confidence = 1.0F;
  return result;
}

}  // namespace voice
}  // namespace cockpit
