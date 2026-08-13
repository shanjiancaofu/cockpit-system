#include "agent/conversation/conversation_state_machine.h"

#include <array>
#include <iostream>

namespace {

using cockpit::voice::ConversationEvent;
using cockpit::voice::ConversationStateMachine;
using cockpit::voice::InteractionState;

constexpr std::array<InteractionState, 13> kStates = {
    InteractionState::kDisabled,     InteractionState::kIdle,
    InteractionState::kWaking,       InteractionState::kListening,
    InteractionState::kRecognizing,  InteractionState::kRouting,
    InteractionState::kExecuting,    InteractionState::kThinking,
    InteractionState::kSpeaking,     InteractionState::kFollowUp,
    InteractionState::kCancelled,    InteractionState::kErrorRecovery,
    InteractionState::kShuttingDown,
};

bool ExpectedTransition(InteractionState from, InteractionState to) {
  if (from == to || from == InteractionState::kShuttingDown) {
    return false;
  }
  if (to == InteractionState::kShuttingDown) {
    return true;
  }
  const bool active = from == InteractionState::kWaking || from == InteractionState::kListening ||
                      from == InteractionState::kRecognizing ||
                      from == InteractionState::kRouting || from == InteractionState::kExecuting ||
                      from == InteractionState::kThinking || from == InteractionState::kSpeaking ||
                      from == InteractionState::kFollowUp;
  if (active && (to == InteractionState::kCancelled || to == InteractionState::kErrorRecovery)) {
    return true;
  }
  switch (from) {
    case InteractionState::kDisabled:
      return to == InteractionState::kIdle;
    case InteractionState::kIdle:
      return to == InteractionState::kWaking || to == InteractionState::kListening ||
             to == InteractionState::kRecognizing;
    case InteractionState::kWaking:
      return to == InteractionState::kListening;
    case InteractionState::kListening:
      return to == InteractionState::kRecognizing || to == InteractionState::kIdle;
    case InteractionState::kRecognizing:
      return to == InteractionState::kRouting;
    case InteractionState::kRouting:
      return to == InteractionState::kExecuting || to == InteractionState::kThinking ||
             to == InteractionState::kSpeaking || to == InteractionState::kIdle;
    case InteractionState::kExecuting:
    case InteractionState::kThinking:
      return to == InteractionState::kSpeaking || to == InteractionState::kFollowUp ||
             to == InteractionState::kIdle;
    case InteractionState::kSpeaking:
      return to == InteractionState::kFollowUp || to == InteractionState::kIdle;
    case InteractionState::kFollowUp:
      return to == InteractionState::kWaking || to == InteractionState::kListening ||
             to == InteractionState::kRecognizing || to == InteractionState::kIdle;
    case InteractionState::kCancelled:
    case InteractionState::kErrorRecovery:
      return to == InteractionState::kIdle;
    case InteractionState::kShuttingDown:
      return false;
  }
  return false;
}

}  // namespace

int main() {
  for (const InteractionState from : kStates) {
    for (const InteractionState to : kStates) {
      if (ConversationStateMachine::IsTransitionAllowed(from, to) != ExpectedTransition(from, to)) {
        std::cerr << "unexpected transition rule from=" << cockpit::voice::ToString(from)
                  << " to=" << cockpit::voice::ToString(to) << '\n';
        return 1;
      }
    }
  }

  ConversationStateMachine machine(true);
  if (machine.snapshot().state != InteractionState::kIdle ||
      !machine.Handle(ConversationEvent::kWakeWordDetected, "wake word detected") ||
      !machine.Handle(ConversationEvent::kWakePromptCompleted, "wake prompt completed") ||
      !machine.Handle(ConversationEvent::kSpeechSegmentReady, "speech endpoint detected") ||
      !machine.Handle(ConversationEvent::kTranscriptReady, "transcript ready") ||
      !machine.Handle(ConversationEvent::kOpenRequestSelected, "open request") ||
      !machine.Handle(ConversationEvent::kResponseReady, "response ready") ||
      !machine.Handle(ConversationEvent::kPlaybackCompleted, "playback completed") ||
      !machine.Handle(ConversationEvent::kFollowUpExpired, "follow-up expired")) {
    std::cerr << "valid conversation path was rejected\n";
    return 1;
  }
  if (machine.Handle(ConversationEvent::kActionSelected, "illegal idle execution")) {
    std::cerr << "illegal conversation path was accepted\n";
    return 1;
  }
  const auto snapshot = machine.snapshot();
  if (snapshot.accepted_transitions != 8 || snapshot.rejected_transitions != 1 ||
      snapshot.last_reason != "follow-up expired") {
    std::cerr << "conversation transition diagnostics are invalid\n";
    return 1;
  }

  ConversationStateMachine disabled(false);
  if (disabled.snapshot().state != InteractionState::kDisabled ||
      !disabled.Handle(ConversationEvent::kShutdownRequested, "shutdown") ||
      disabled.Handle(ConversationEvent::kEnabled, "illegal restart")) {
    std::cerr << "disabled/shutdown behavior is invalid\n";
    return 1;
  }

  std::cout << "conversation state machine tests passed\n";
  return 0;
}
