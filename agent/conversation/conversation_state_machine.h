#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace voice {

enum class InteractionState {
  kDisabled,
  kIdle,
  kWaking,
  kListening,
  kRecognizing,
  kRouting,
  kExecuting,
  kThinking,
  kSpeaking,
  kFollowUp,
  kCancelled,
  kErrorRecovery,
  kShuttingDown,
};

enum class ConversationEvent {
  kEnabled,
  kWakeWordDetected,
  kWakePromptCompleted,
  kSpeechSegmentReady,
  kTranscriptReady,
  kActionSelected,
  kOpenRequestSelected,
  kResponseReady,
  kPlaybackCompleted,
  kRequestCompleted,
  kFollowUpExpired,
  kCancelRequested,
  kFailure,
  kRecoveryCompleted,
  kShutdownRequested,
};

struct ConversationStateSnapshot {
  InteractionState state = InteractionState::kDisabled;
  std::uint64_t accepted_transitions = 0;
  std::uint64_t rejected_transitions = 0;
  std::string last_reason;
};

class ConversationStateMachine {
 public:
  explicit ConversationStateMachine(bool enabled);

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(ConversationStateMachine);

  bool Handle(ConversationEvent event, std::string reason);
  ConversationStateSnapshot snapshot() const;

  static bool IsTransitionAllowed(InteractionState from, InteractionState to);
  static bool IsActive(InteractionState state);

 private:
  mutable std::mutex mutex_;
  ConversationStateSnapshot snapshot_;
};

const char* ToString(InteractionState state);
const char* ToString(ConversationEvent event);

}  // namespace voice
}  // namespace cockpit
