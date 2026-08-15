#pragma once

#include <memory>

#include "agent/speech/vad/voice_activity_detector.h"

namespace cockpit {
namespace agent {

std::unique_ptr<audio::VoiceActivityDetector> CreateSherpaVoiceActivityDetector();

}  // namespace agent
}  // namespace cockpit
