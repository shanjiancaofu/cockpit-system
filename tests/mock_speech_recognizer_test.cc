#include "cockpit/modules/voice/asr/mock_speech_recognizer.h"

#include <iostream>

int main() {
  cockpit::voice::MockSpeechRecognizer recognizer;
  cockpit::audio::SpeechSegment empty;
  const auto empty_result = recognizer.Recognize(empty);
  if (empty_result.success || empty_result.error.empty()) {
    std::cerr << "mock recognizer accepted an empty segment\n";
    return 1;
  }

  cockpit::audio::SpeechSegment segment;
  segment.samples.resize(cockpit::audio::AudioFrame::kSampleCount * 5U, 1000);
  const auto result = recognizer.Recognize(segment);
  if (!result.success || result.provider != "mock" || result.confidence != 1.0F ||
      result.text != "mock transcript duration_ms=100") {
    std::cerr << "mock recognizer result is not deterministic\n";
    return 1;
  }
  std::cout << "mock speech recognizer tests passed\n";
  return 0;
}
