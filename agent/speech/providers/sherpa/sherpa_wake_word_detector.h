#pragma once

#include <memory>

#include "agent/speech/kws/wake_word_detector.h"
#include "cockpit/core/config/system_config.h"

namespace cockpit {
namespace agent {

std::unique_ptr<WakeWordDetector> CreateSherpaWakeWordDetector(const config::KwsConfig& config);

}  // namespace agent
}  // namespace cockpit
