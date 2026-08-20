#pragma once

#include <memory>

#include "cockpit/modules/voice/actions/hmi_command_provider.h"

namespace cockpit {
namespace voice {

class AudioFocusController {
 public:
  virtual ~AudioFocusController() = default;
  virtual bool AcquireTts() = 0;
  virtual void ReleaseTts() = 0;
};

std::unique_ptr<AudioFocusController> CreateHmiAudioFocusController(
    std::unique_ptr<HmiCommandProvider> provider);

}  // namespace voice
}  // namespace cockpit
