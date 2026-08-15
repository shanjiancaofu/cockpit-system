#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "agent/conversation/conversation_state_machine.h"
#include "cockpit/core/base/macros.h"
#include "cockpit/core/event/event_queue.h"
#include "cockpit/modules/voice/actions/action_dispatcher.h"
#include "cockpit/modules/voice/assistant/speech_transcript.h"
#include "cockpit/modules/voice/assistant/voice_assistant.h"
#include "cockpit/modules/voice/responses/voice_response_sink.h"

namespace cockpit {
namespace voice {

struct VoiceResponse {
  std::uint64_t id = 0;
  std::uint64_t timestamp_ms = 0;
  std::uint64_t transcript_id = 0;
  std::string transcript_text;
  VoiceIntent intent = VoiceIntent::kUnknown;
  VoiceAction action = VoiceAction::kNone;
  ActionExecutionStatus action_status = ActionExecutionStatus::kNotRequested;
  std::string action_message;
  std::string response_text;
};

struct VoiceInteractionMetrics {
  std::uint64_t transcripts_received = 0;
  std::uint64_t transcript_events_dropped = 0;
  std::uint64_t responses_published = 0;
  std::uint64_t unknown_intents = 0;
  std::uint64_t processing_errors = 0;
  std::uint64_t upstream_reconnects = 0;
  std::uint64_t actions_attempted = 0;
  std::uint64_t actions_succeeded = 0;
  std::uint64_t actions_failed = 0;
  std::uint64_t requests_interrupted = 0;
  std::uint64_t assistant_timeouts = 0;
  std::uint64_t assistant_failures = 0;
  std::uint64_t action_timeouts = 0;
  std::uint64_t state_transitions = 0;
  std::uint64_t rejected_state_transitions = 0;
  VoiceOutputMetrics output;
};

struct VoiceInterruptResult {
  bool active_request_interrupted = false;
  std::uint64_t queued_transcripts_discarded = 0;
};

struct VoiceInteractionStatus {
  InteractionState state = InteractionState::kDisabled;
  VoiceInteractionMetrics metrics;
  std::optional<VoiceResponse> latest_response;
  std::string state_reason;
  std::string last_error;
};

class VoiceInteractionService {
 public:
  using ResponseObserver = std::function<void(const VoiceResponse&)>;

  VoiceInteractionService(bool enabled, std::unique_ptr<VoiceAssistant> assistant,
                          std::unique_ptr<ActionDispatcher> dispatcher,
                          std::unique_ptr<VoiceResponseSink> output = nullptr,
                          ResponseObserver response_observer = nullptr,
                          std::chrono::milliseconds assistant_timeout = std::chrono::seconds(10),
                          std::chrono::milliseconds action_timeout = std::chrono::seconds(3),
                          std::chrono::milliseconds follow_up_window = std::chrono::seconds(8));
  ~VoiceInteractionService();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(VoiceInteractionService);

  bool Start();
  void Stop();
  event::EventQueuePushResult SubmitTranscript(const SpeechTranscript& transcript);
  std::optional<VoiceResponse> HandleTranscript(const SpeechTranscript& transcript);
  VoiceInterruptResult Interrupt();
  bool NotifyWakeWordDetected();
  bool NotifyWakePromptCompleted();
  bool NotifyWakePromptFailed(std::string error);
  InteractionState state() const;
  bool WaitForResponse(std::uint64_t after_id, std::chrono::milliseconds timeout,
                       VoiceResponse* response) const;
  bool WaitForTranscript(std::uint64_t after_id, std::chrono::milliseconds timeout,
                         SpeechTranscript* transcript) const;
  VoiceInteractionStatus status() const;
  void RecordUpstreamReconnect();
  void SetLastError(std::string error);

 private:
  bool BeginRequest();
  void BeginAssistantCall();
  void EndAssistantCall();
  void CancelAssistantCall();
  std::shared_ptr<ActionCancellation> BeginActionCall();
  void EndActionCall(const std::shared_ptr<ActionCancellation>& cancellation);
  void CancelActionCall();
  void HandleOutputResult(std::uint64_t request_generation, VoiceOutputResult result);
  void ExpireFollowUpIfNeeded();
  void InvalidateOutputLifecycle();
  void RecoverFromError(const std::string& reason, std::uint64_t request_generation,
                        std::optional<VoiceResponse> response = std::nullopt,
                        bool play_prompt = false);
  bool SubmitOutput(VoiceResponse response, std::uint64_t request_generation, bool recovery);
  void ReturnToIdle(const std::string& reason);
  VoiceResponse PublishResponse(VoiceResponse response);
  void ProcessLoop();

  const bool enabled_;
  const std::unique_ptr<VoiceAssistant> assistant_;
  const std::unique_ptr<ActionDispatcher> dispatcher_;
  const std::unique_ptr<VoiceResponseSink> output_;
  const ResponseObserver response_observer_;
  const std::chrono::milliseconds assistant_timeout_;
  const std::chrono::milliseconds action_timeout_;
  const std::chrono::milliseconds follow_up_window_;
  mutable std::mutex processing_mutex_;
  std::mutex provider_lifecycle_mutex_;
  bool assistant_call_active_ = false;
  bool assistant_cancel_requested_ = false;
  bool action_call_active_ = false;
  bool action_cancel_requested_ = false;
  std::shared_ptr<ActionCancellation> active_action_cancellation_;
  event::EventQueue<SpeechTranscript> transcript_events_{32};
  std::atomic<bool> worker_running_{false};
  std::unique_ptr<std::thread> worker_;
  ConversationStateMachine state_machine_;
  std::atomic<std::uint64_t> transcripts_received_{0};
  std::atomic<std::uint64_t> responses_published_{0};
  std::atomic<std::uint64_t> unknown_intents_{0};
  std::atomic<std::uint64_t> processing_errors_{0};
  std::atomic<std::uint64_t> upstream_reconnects_{0};
  std::atomic<std::uint64_t> actions_attempted_{0};
  std::atomic<std::uint64_t> actions_succeeded_{0};
  std::atomic<std::uint64_t> actions_failed_{0};
  std::atomic<std::uint64_t> requests_interrupted_{0};
  std::atomic<std::uint64_t> assistant_timeouts_{0};
  std::atomic<std::uint64_t> assistant_failures_{0};
  std::atomic<std::uint64_t> action_timeouts_{0};
  std::atomic<std::uint64_t> interrupt_generation_{0};
  mutable std::mutex output_mutex_;
  std::uint64_t active_output_request_id_ = 0;
  std::uint64_t active_output_generation_ = 0;
  bool active_output_recovery_ = false;
  std::optional<std::chrono::steady_clock::time_point> follow_up_deadline_;
  mutable std::mutex response_mutex_;
  mutable std::condition_variable response_changed_;
  std::deque<VoiceResponse> response_history_;
  std::uint64_t response_version_ = 0;
  std::string last_error_;
  mutable std::mutex transcript_mutex_;
  mutable std::condition_variable transcript_changed_;
  std::deque<SpeechTranscript> transcript_history_;
};

}  // namespace voice
}  // namespace cockpit
