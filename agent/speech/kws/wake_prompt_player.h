#pragma once

#include <string>

namespace cockpit {
namespace agent {

class WakePromptPlayer {
 public:
  virtual ~WakePromptPlayer() = default;

  virtual bool Play(std::string* error) = 0;
};

class NoopWakePromptPlayer final : public WakePromptPlayer {
 public:
  bool Play(std::string* error) override;
};

}  // namespace agent
}  // namespace cockpit
