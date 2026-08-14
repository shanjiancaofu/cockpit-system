#include "agent/speech/kws/wake_prompt_player.h"

namespace cockpit {
namespace agent {

bool NoopWakePromptPlayer::Play(std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

}  // namespace agent
}  // namespace cockpit
