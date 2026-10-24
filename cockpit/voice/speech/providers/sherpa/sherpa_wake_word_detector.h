#pragma once

#include <memory>

#include "cockpit/exchange/foundation/config/system_config.h"
#include "cockpit/voice/speech/kws/wake_word_detector.h"

namespace cockpit {
namespace agent {

std::unique_ptr<WakeWordDetector> CreateSherpaWakeWordDetector(const config::KwsConfig& config);

}  // namespace agent
}  // namespace cockpit
