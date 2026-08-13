#pragma once

#include "agent/speech/asr/speech_recognizer.h"

namespace cockpit {
namespace voice {

class MockSpeechRecognizer final : public SpeechRecognizer {
 public:
  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment,
                                    std::chrono::steady_clock::time_point deadline) override;
  void Cancel() override {
  }
};

}  // namespace voice
}  // namespace cockpit
