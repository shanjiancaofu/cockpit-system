#pragma once

#include "cockpit/voice/domain/actions/action_dispatcher.h"

namespace cockpit {
namespace voice {

class MockActionDispatcher final : public ActionDispatcher {
 public:
  ActionExecutionResult Execute(VoiceAction action, const ActionExecutionContext& context) override;
};

}  // namespace voice
}  // namespace cockpit
