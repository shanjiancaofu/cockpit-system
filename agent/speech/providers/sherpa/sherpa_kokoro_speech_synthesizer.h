#pragma once

#include <memory>

#include "agent/speech/tts/speech_synthesizer.h"
#include "cockpit/core/config/system_config.h"

namespace cockpit {
namespace voice {

std::unique_ptr<SpeechSynthesizer> CreateSherpaKokoroSpeechSynthesizer(
    const config::TtsConfig& config);

}  // namespace voice
}  // namespace cockpit
