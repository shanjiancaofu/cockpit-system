#pragma once

#include "agent/speech/tts/speech_synthesizer.h"

namespace cockpit {
namespace voice {

class MockSpeechSynthesizer final : public SpeechSynthesizer {
 public:
  SpeechSynthesisResult Synthesize(const std::string& text) override;
};

}  // namespace voice
}  // namespace cockpit
