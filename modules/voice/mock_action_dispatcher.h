#pragma once

#include "modules/voice/action_dispatcher.h"

namespace cockpit {
namespace voice {

class MockActionDispatcher final : public ActionDispatcher {
 public:
  ActionExecutionResult Execute(VoiceAction action) override;
};

}  // namespace voice
}  // namespace cockpit
