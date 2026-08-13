#include "agent/conversation/conversation_state_machine.h"

#include <optional>
#include <utility>

namespace cockpit {
namespace voice {
namespace {

bool IsRecoveryTarget(InteractionState state) {
  return state == InteractionState::kCancelled || state == InteractionState::kErrorRecovery ||
         state == InteractionState::kShuttingDown;
}

std::optional<InteractionState> NextState(InteractionState state, ConversationEvent event) {
  switch (event) {
    case ConversationEvent::kEnabled:
      return state == InteractionState::kDisabled ? std::optional(InteractionState::kIdle)
                                                  : std::nullopt;
    case ConversationEvent::kWakeWordDetected:
      return state == InteractionState::kIdle || state == InteractionState::kFollowUp
                 ? std::optional(InteractionState::kWaking)
                 : std::nullopt;
    case ConversationEvent::kWakePromptCompleted:
      return state == InteractionState::kWaking ? std::optional(InteractionState::kListening)
                                                : std::nullopt;
    case ConversationEvent::kSpeechSegmentReady:
      return state == InteractionState::kIdle || state == InteractionState::kListening ||
                     state == InteractionState::kFollowUp
                 ? std::optional(InteractionState::kRecognizing)
                 : std::nullopt;
    case ConversationEvent::kTranscriptReady:
      return state == InteractionState::kRecognizing ? std::optional(InteractionState::kRouting)
                                                     : std::nullopt;
    case ConversationEvent::kActionSelected:
      return state == InteractionState::kRouting ? std::optional(InteractionState::kExecuting)
                                                 : std::nullopt;
    case ConversationEvent::kOpenRequestSelected:
      return state == InteractionState::kRouting ? std::optional(InteractionState::kThinking)
                                                 : std::nullopt;
    case ConversationEvent::kResponseReady:
      return state == InteractionState::kRouting || state == InteractionState::kExecuting ||
                     state == InteractionState::kThinking
                 ? std::optional(InteractionState::kSpeaking)
                 : std::nullopt;
    case ConversationEvent::kPlaybackCompleted:
      return state == InteractionState::kSpeaking ? std::optional(InteractionState::kFollowUp)
                                                  : std::nullopt;
    case ConversationEvent::kRequestCompleted:
      return state == InteractionState::kRouting || state == InteractionState::kExecuting ||
                     state == InteractionState::kThinking || state == InteractionState::kSpeaking
                 ? std::optional(InteractionState::kIdle)
                 : std::nullopt;
    case ConversationEvent::kFollowUpExpired:
      return state == InteractionState::kFollowUp ? std::optional(InteractionState::kIdle)
                                                  : std::nullopt;
    case ConversationEvent::kCancelRequested:
      return ConversationStateMachine::IsActive(state) ? std::optional(InteractionState::kCancelled)
                                                       : std::nullopt;
    case ConversationEvent::kFailure:
      return ConversationStateMachine::IsActive(state)
                 ? std::optional(InteractionState::kErrorRecovery)
                 : std::nullopt;
    case ConversationEvent::kRecoveryCompleted:
      return state == InteractionState::kCancelled || state == InteractionState::kErrorRecovery
                 ? std::optional(InteractionState::kIdle)
                 : std::nullopt;
    case ConversationEvent::kShutdownRequested:
      return state != InteractionState::kShuttingDown
                 ? std::optional(InteractionState::kShuttingDown)
                 : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

ConversationStateMachine::ConversationStateMachine(bool enabled) {
  snapshot_.state = enabled ? InteractionState::kIdle : InteractionState::kDisabled;
  snapshot_.last_reason = enabled ? "voice interaction initialized" : "voice interaction disabled";
}

bool ConversationStateMachine::Handle(ConversationEvent event, std::string reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto next = NextState(snapshot_.state, event);
  if (!next.has_value() || !IsTransitionAllowed(snapshot_.state, *next)) {
    ++snapshot_.rejected_transitions;
    return false;
  }
  snapshot_.state = *next;
  snapshot_.last_reason = std::move(reason);
  ++snapshot_.accepted_transitions;
  return true;
}

ConversationStateSnapshot ConversationStateMachine::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool ConversationStateMachine::IsTransitionAllowed(InteractionState from, InteractionState to) {
  if (from == to || from == InteractionState::kShuttingDown) {
    return false;
  }
  if (to == InteractionState::kShuttingDown) {
    return true;
  }
  if (IsActive(from) && IsRecoveryTarget(to)) {
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

bool ConversationStateMachine::IsActive(InteractionState state) {
  switch (state) {
    case InteractionState::kWaking:
    case InteractionState::kListening:
    case InteractionState::kRecognizing:
    case InteractionState::kRouting:
    case InteractionState::kExecuting:
    case InteractionState::kThinking:
    case InteractionState::kSpeaking:
    case InteractionState::kFollowUp:
      return true;
    case InteractionState::kDisabled:
    case InteractionState::kIdle:
    case InteractionState::kCancelled:
    case InteractionState::kErrorRecovery:
    case InteractionState::kShuttingDown:
      return false;
  }
  return false;
}

const char* ToString(InteractionState state) {
  switch (state) {
    case InteractionState::kDisabled:
      return "disabled";
    case InteractionState::kIdle:
      return "idle";
    case InteractionState::kWaking:
      return "waking";
    case InteractionState::kListening:
      return "listening";
    case InteractionState::kRecognizing:
      return "recognizing";
    case InteractionState::kRouting:
      return "routing";
    case InteractionState::kExecuting:
      return "executing";
    case InteractionState::kThinking:
      return "thinking";
    case InteractionState::kSpeaking:
      return "speaking";
    case InteractionState::kFollowUp:
      return "follow_up";
    case InteractionState::kCancelled:
      return "cancelled";
    case InteractionState::kErrorRecovery:
      return "error_recovery";
    case InteractionState::kShuttingDown:
      return "shutting_down";
  }
  return "unknown";
}

const char* ToString(ConversationEvent event) {
  switch (event) {
    case ConversationEvent::kEnabled:
      return "enabled";
    case ConversationEvent::kWakeWordDetected:
      return "wake_word_detected";
    case ConversationEvent::kWakePromptCompleted:
      return "wake_prompt_completed";
    case ConversationEvent::kSpeechSegmentReady:
      return "speech_segment_ready";
    case ConversationEvent::kTranscriptReady:
      return "transcript_ready";
    case ConversationEvent::kActionSelected:
      return "action_selected";
    case ConversationEvent::kOpenRequestSelected:
      return "open_request_selected";
    case ConversationEvent::kResponseReady:
      return "response_ready";
    case ConversationEvent::kPlaybackCompleted:
      return "playback_completed";
    case ConversationEvent::kRequestCompleted:
      return "request_completed";
    case ConversationEvent::kFollowUpExpired:
      return "follow_up_expired";
    case ConversationEvent::kCancelRequested:
      return "cancel_requested";
    case ConversationEvent::kFailure:
      return "failure";
    case ConversationEvent::kRecoveryCompleted:
      return "recovery_completed";
    case ConversationEvent::kShutdownRequested:
      return "shutdown_requested";
  }
  return "unknown";
}

}  // namespace voice
}  // namespace cockpit
