#pragma once

#include <memory>

#include "agent/speech/asr/speech_recognizer.h"

namespace cockpit {
namespace voice {

std::unique_ptr<SpeechRecognizer> CreateSherpaSenseVoiceRecognizer();

}  // namespace voice
}  // namespace cockpit
