#pragma once

#include "agent/speech/tts/speech_synthesizer.h"

namespace cockpit {
namespace voice {

class MockSpeechSynthesizer final : public SpeechSynthesizer {
 public:
  SpeechSynthesisResult Synthesize(const std::string& text,
                                   std::chrono::steady_clock::time_point deadline) override;
  void Cancel() override {
  }
};

}  // namespace voice
}  // namespace cockpit
