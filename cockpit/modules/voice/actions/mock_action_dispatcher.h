#pragma once

#include "cockpit/modules/voice/actions/action_dispatcher.h"

namespace cockpit {
namespace voice {

class MockActionDispatcher final : public ActionDispatcher {
 public:
  ActionExecutionResult Execute(VoiceAction action,
                                std::chrono::steady_clock::time_point deadline) override;
};

}  // namespace voice
}  // namespace cockpit
