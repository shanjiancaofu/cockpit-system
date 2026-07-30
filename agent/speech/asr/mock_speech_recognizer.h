#pragma once

#include "agent/speech/asr/speech_recognizer.h"

namespace cockpit {
namespace voice {

class MockSpeechRecognizer final : public SpeechRecognizer {
 public:
  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment) override;
};

}  // namespace voice
}  // namespace cockpit
