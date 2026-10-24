#pragma once

#include <memory>

#include "cockpit/exchange/foundation/config/system_config.h"
#include "cockpit/voice/speech/tts/speech_synthesizer.h"

namespace cockpit {
namespace voice {

std::unique_ptr<SpeechSynthesizer> CreateSherpaKokoroSpeechSynthesizer(
    const config::TtsConfig& config);

}  // namespace voice
}  // namespace cockpit
