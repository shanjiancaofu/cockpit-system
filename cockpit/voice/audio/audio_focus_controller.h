#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace cockpit {
namespace voice {

class AudioFocusController {
 public:
  virtual ~AudioFocusController() = default;
  virtual bool AcquireTts() = 0;
  virtual void ReleaseTts() = 0;
};

std::unique_ptr<AudioFocusController> CreateMediaAudioFocusController(
    const std::string& address, std::chrono::milliseconds timeout = std::chrono::milliseconds(900));

}  // namespace voice
}  // namespace cockpit
