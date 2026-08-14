#include "agent/interaction/voice_interaction_service.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <stdexcept>
#include <utility>

namespace cockpit {
namespace voice {
namespace {

constexpr char kRecoveryPrompt[] = "Sorry, I couldn't complete that request. Please try again.";

}  // namespace

VoiceInteractionService::VoiceInteractionService(
    bool enabled, std::unique_ptr<VoiceAssistant> assistant,
    std::unique_ptr<ActionDispatcher> dispatcher, std::unique_ptr<VoiceResponseSink> output,
    ResponseObserver response_observer, std::chrono::milliseconds assistant_timeout,
    std::chrono::milliseconds action_timeout, std::chrono::milliseconds follow_up_window)
    : enabled_(enabled),
      assistant_(std::move(assistant)),
      dispatcher_(std::move(dispatcher)),
      output_(std::move(output)),
      response_observer_(std::move(response_observer)),
      assistant_timeout_(assistant_timeout),
      action_timeout_(action_timeout),
      follow_up_window_(follow_up_window),
      state_machine_(enabled) {
  if (assistant_timeout_ <= std::chrono::milliseconds::zero() ||
      action_timeout_ <= std::chrono::milliseconds::zero() ||
      follow_up_window_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("voice assistant, action, and follow-up timeouts must be positive");
  }
}

VoiceInteractionService::~VoiceInteractionService() {
  Stop();
}

bool VoiceInteractionService::Start() {
  if (!enabled_ || assistant_ == nullptr) {
    return false;
  }
  if (state_machine_.snapshot().state == InteractionState::kShuttingDown) {
    return false;
  }
  bool expected = false;
  if (!worker_running_.compare_exchange_strong(expected, true)) {
    return true;
  }
  transcript_events_.Reset();
  worker_ = std::make_unique<std::thread>(&VoiceInteractionService::ProcessLoop, this);
  return true;
}

void VoiceInteractionService::Stop() {
  if (state_machine_.snapshot().state == InteractionState::kShuttingDown) {
    return;
  }
  worker_running_.store(false);
  state_machine_.Handle(ConversationEvent::kShutdownRequested, "voice interaction stopping");
  interrupt_generation_.fetch_add(1U);
  InvalidateOutputLifecycle();
  transcript_events_.Close();
  transcript_events_.DiscardPending();
  CancelAssistantCall();
  CancelActionCall();
  if (output_ != nullptr) {
    output_->Stop();
  }
  if (worker_ != nullptr && worker_->joinable()) {
    worker_->join();
  }
  worker_.reset();
}

event::EventQueuePushResult VoiceInteractionService::SubmitTranscript(
    const SpeechTranscript& transcript) {
  if (!enabled_ || assistant_ == nullptr || !worker_running_.load()) {
    return event::EventQueuePushResult::kClosed;
  }
  const event::EventQueuePushResult result = transcript_events_.Push(transcript);
  if (result == event::EventQueuePushResult::kAccepted) {
    {
      std::lock_guard<std::mutex> lock(transcript_mutex_);
      transcript_history_.push_back(transcript);
      while (transcript_history_.size() > 32U) {
        transcript_history_.pop_front();
      }
    }
    transcript_changed_.notify_all();
  }
  return result;
}

std::optional<VoiceResponse> VoiceInteractionService::HandleTranscript(
    const SpeechTranscript& transcript) {
  if (!enabled_ || assistant_ == nullptr) {
    return std::nullopt;
  }
  const std::uint64_t request_generation = interrupt_generation_.load();
  std::lock_guard<std::mutex> processing_lock(processing_mutex_);
  if (request_generation != interrupt_generation_.load()) {
    SetLastError("voice request interrupted");
    return std::nullopt;
  }
  if (transcript.text.empty()) {
    processing_errors_.fetch_add(1U);
    SetLastError("transcript text is empty");
    return std::nullopt;
  }

  if (!BeginRequest()) {
    SetLastError("another voice session is active or shutting down");
    return std::nullopt;
  }
  transcripts_received_.fetch_add(1U);
  const auto provider_started = std::chrono::steady_clock::now();
  const auto provider_deadline = provider_started + assistant_timeout_;
  const auto provider_remaining = provider_deadline - std::chrono::steady_clock::now();
  const auto watchdog_deadline = std::chrono::system_clock::now() + provider_remaining;
  VoiceAssistantResult result;
  std::mutex provider_mutex;
  std::condition_variable provider_changed;
  bool provider_finished = false;
  std::atomic_bool provider_timed_out{false};
  BeginAssistantCall();
  std::thread provider_watchdog([&] {
    std::unique_lock<std::mutex> lock(provider_mutex);
    if (!provider_changed.wait_until(lock, watchdog_deadline, [&] {
          return provider_finished;
        })) {
      provider_timed_out.store(true);
      CancelAssistantCall();
    }
  });
  std::string provider_error;
  try {
    result = assistant_->HandleTranscript(transcript, provider_deadline);
  } catch (const std::exception& exception) {
    provider_error = exception.what();
  } catch (...) {
    provider_error = "voice assistant provider failed";
  }
  EndAssistantCall();
  {
    std::lock_guard<std::mutex> lock(provider_mutex);
    provider_finished = true;
  }
  provider_changed.notify_all();
  provider_watchdog.join();
  if (!provider_error.empty()) {
    if (request_generation != interrupt_generation_.load()) {
      return std::nullopt;
    }
    processing_errors_.fetch_add(1U);
    std::string recovery_reason;
    if (provider_timed_out.load()) {
      assistant_timeouts_.fetch_add(1U);
      recovery_reason = "voice assistant provider timed out";
    } else {
      assistant_failures_.fetch_add(1U);
      recovery_reason = provider_error;
    }
    SetLastError(recovery_reason);
    VoiceResponse recovery;
    recovery.timestamp_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
    recovery.transcript_id = transcript.id;
    recovery.transcript_text = transcript.text;
    RecoverFromError(recovery_reason, request_generation, std::move(recovery), true);
    return std::nullopt;
  }

  if (request_generation != interrupt_generation_.load()) {
    return std::nullopt;
  }
  if (provider_timed_out.load() || std::chrono::steady_clock::now() > provider_deadline) {
    processing_errors_.fetch_add(1U);
    assistant_timeouts_.fetch_add(1U);
    SetLastError("voice assistant provider timed out");
    VoiceResponse recovery;
    recovery.timestamp_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
    recovery.transcript_id = transcript.id;
    recovery.transcript_text = transcript.text;
    RecoverFromError("voice assistant provider timed out", request_generation, std::move(recovery),
                     true);
    return std::nullopt;
  }

  if (!state_machine_.Handle(ConversationEvent::kTranscriptReady, "final transcript recognized")) {
    SetLastError("invalid conversation transition to routing");
    return std::nullopt;
  }

  try {
    VoiceResponse response;
    response.timestamp_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
    response.transcript_id = transcript.id;
    response.transcript_text = transcript.text;
    response.intent = result.intent;
    response.action = result.action;
    response.response_text = result.response_text;
    if (result.intent == VoiceIntent::kUnknown) {
      unknown_intents_.fetch_add(1U);
    }
    if (result.action != VoiceAction::kNone) {
      const auto action_cancellation =
          dispatcher_ != nullptr ? BeginActionCall() : std::shared_ptr<ActionCancellation>{};
      if (!state_machine_.Handle(ConversationEvent::kActionSelected,
                                 "deterministic action selected")) {
        if (action_cancellation != nullptr) {
          EndActionCall(action_cancellation);
        }
        throw std::runtime_error("invalid conversation transition to executing");
      }
      actions_attempted_.fetch_add(1U);
      if (dispatcher_ == nullptr) {
        response.action_status = ActionExecutionStatus::kNotImplemented;
        response.action_message = "No action dispatcher is configured.";
        response.response_text = response.action_message;
        actions_failed_.fetch_add(1U);
      } else {
        const auto action_deadline = std::chrono::steady_clock::now() + action_timeout_;
        const auto action_remaining = action_deadline - std::chrono::steady_clock::now();
        const auto action_watchdog_deadline = std::chrono::system_clock::now() + action_remaining;
        std::mutex action_mutex;
        std::condition_variable action_changed;
        bool action_finished = false;
        std::atomic_bool action_timed_out{false};
        const ActionExecutionContext action_context{action_deadline, action_cancellation};
        std::thread action_watchdog([&] {
          std::unique_lock<std::mutex> lock(action_mutex);
          if (!action_changed.wait_until(lock, action_watchdog_deadline, [&] {
                return action_finished;
              })) {
            action_timed_out.store(true);
            CancelActionCall();
          }
        });
        ActionExecutionResult execution;
        try {
          execution = dispatcher_->Execute(result.action, action_context);
        } catch (const std::exception& exception) {
          execution = {ActionExecutionStatus::kFailed, exception.what()};
        } catch (...) {
          execution = {ActionExecutionStatus::kFailed, "Action provider failed."};
        }
        EndActionCall(action_cancellation);
        {
          std::lock_guard<std::mutex> lock(action_mutex);
          action_finished = true;
        }
        action_changed.notify_all();
        action_watchdog.join();
        if (request_generation != interrupt_generation_.load()) {
          return std::nullopt;
        }
        if (action_timed_out.load() || std::chrono::steady_clock::now() > action_deadline) {
          action_timeouts_.fetch_add(1U);
          actions_failed_.fetch_add(1U);
          processing_errors_.fetch_add(1U);
          response.action_status = ActionExecutionStatus::kFailed;
          response.action_message = "Action execution deadline exceeded.";
          SetLastError(response.action_message);
          RecoverFromError(response.action_message, request_generation, std::move(response), true);
          return std::nullopt;
        }
        response.action_status = execution.status;
        response.action_message = execution.message;
        if (!execution.message.empty()) {
          response.response_text = execution.message;
        }
        if (execution.status == ActionExecutionStatus::kSucceeded) {
          actions_succeeded_.fetch_add(1U);
        } else {
          actions_failed_.fetch_add(1U);
          if (execution.status == ActionExecutionStatus::kFailed) {
            processing_errors_.fetch_add(1U);
            SetLastError(execution.message.empty() ? "Action provider failed." : execution.message);
            RecoverFromError(
                execution.message.empty() ? "Action provider failed." : execution.message,
                request_generation, std::move(response), true);
            return std::nullopt;
          }
        }
      }
    } else if (!state_machine_.Handle(ConversationEvent::kOpenRequestSelected,
                                      "open response selected")) {
      throw std::runtime_error("invalid conversation transition to thinking");
    }
    response = PublishResponse(std::move(response));
    if (!SubmitOutput(response, request_generation, false)) {
      throw std::runtime_error("voice output queue rejected the response");
    }
    return response;
  } catch (const std::exception& exception) {
    processing_errors_.fetch_add(1U);
    SetLastError(exception.what());
    RecoverFromError(exception.what(), request_generation);
    return std::nullopt;
  }
}

VoiceInterruptResult VoiceInteractionService::Interrupt() {
  VoiceInterruptResult result;
  interrupt_generation_.fetch_add(1U);
  bool output_active = false;
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_active = active_output_request_id_ != 0U;
  }
  InvalidateOutputLifecycle();
  result.queued_transcripts_discarded = transcript_events_.DiscardPending();
  result.active_request_interrupted =
      output_active || ConversationStateMachine::IsActive(state_machine_.snapshot().state);
  if (result.active_request_interrupted) {
    requests_interrupted_.fetch_add(1U);
    if (ConversationStateMachine::IsActive(state_machine_.snapshot().state)) {
      state_machine_.Handle(ConversationEvent::kCancelRequested, "voice request interrupted");
    }
    SetLastError("voice request interrupted");
    CancelAssistantCall();
    CancelActionCall();
    if (output_ != nullptr) {
      output_->Interrupt();
    }
    ReturnToIdle("voice interrupt cleanup completed");
  }
  return result;
}

bool VoiceInteractionService::NotifyWakeWordDetected() {
  if (!enabled_ || !worker_running_.load()) {
    return false;
  }
  const bool accepted =
      state_machine_.Handle(ConversationEvent::kWakeWordDetected, "wake word detected");
  if (!accepted) {
    SetLastError("wake word ignored in current interaction state");
  }
  return accepted;
}

bool VoiceInteractionService::NotifyWakePromptCompleted() {
  if (!enabled_ || !worker_running_.load()) {
    return false;
  }
  const bool accepted =
      state_machine_.Handle(ConversationEvent::kWakePromptCompleted, "wake prompt completed");
  if (!accepted) {
    SetLastError("wake prompt completion ignored in current interaction state");
  }
  return accepted;
}

InteractionState VoiceInteractionService::state() const {
  return state_machine_.snapshot().state;
}

bool VoiceInteractionService::WaitForResponse(std::uint64_t after_id,
                                              std::chrono::milliseconds timeout,
                                              VoiceResponse* response) const {
  std::unique_lock<std::mutex> lock(response_mutex_);
  response_changed_.wait_until(lock, std::chrono::system_clock::now() + timeout, [this, after_id] {
    return !response_history_.empty() && response_history_.back().id > after_id;
  });
  const auto next = std::find_if(response_history_.begin(), response_history_.end(),
                                 [after_id](const VoiceResponse& value) {
                                   return value.id > after_id;
                                 });
  if (next == response_history_.end()) {
    return false;
  }
  if (response != nullptr) {
    *response = *next;
  }
  return true;
}

bool VoiceInteractionService::WaitForTranscript(std::uint64_t after_id,
                                                std::chrono::milliseconds timeout,
                                                SpeechTranscript* transcript) const {
  std::unique_lock<std::mutex> lock(transcript_mutex_);
  transcript_changed_.wait_until(
      lock, std::chrono::system_clock::now() + timeout, [this, after_id] {
        return !transcript_history_.empty() && transcript_history_.back().id > after_id;
      });
  const auto next = std::find_if(transcript_history_.begin(), transcript_history_.end(),
                                 [after_id](const SpeechTranscript& value) {
                                   return value.id > after_id;
                                 });
  if (next == transcript_history_.end()) {
    return false;
  }
  if (transcript != nullptr) {
    *transcript = *next;
  }
  return true;
}

VoiceInteractionStatus VoiceInteractionService::status() const {
  VoiceInteractionStatus result;
  const ConversationStateSnapshot state = state_machine_.snapshot();
  result.state = state.state;
  result.state_reason = state.last_reason;
  result.metrics.transcripts_received = transcripts_received_.load();
  result.metrics.transcript_events_dropped = transcript_events_.DropCount();
  result.metrics.responses_published = responses_published_.load();
  result.metrics.unknown_intents = unknown_intents_.load();
  result.metrics.processing_errors = processing_errors_.load();
  result.metrics.upstream_reconnects = upstream_reconnects_.load();
  result.metrics.actions_attempted = actions_attempted_.load();
  result.metrics.actions_succeeded = actions_succeeded_.load();
  result.metrics.actions_failed = actions_failed_.load();
  result.metrics.requests_interrupted = requests_interrupted_.load();
  result.metrics.assistant_timeouts = assistant_timeouts_.load();
  result.metrics.assistant_failures = assistant_failures_.load();
  result.metrics.action_timeouts = action_timeouts_.load();
  result.metrics.state_transitions = state.accepted_transitions;
  result.metrics.rejected_state_transitions = state.rejected_transitions;
  if (output_ != nullptr) {
    result.metrics.output = output_->metrics();
  }
  std::lock_guard<std::mutex> lock(response_mutex_);
  if (!response_history_.empty()) {
    result.latest_response = response_history_.back();
  }
  result.last_error = last_error_;
  return result;
}

bool VoiceInteractionService::BeginRequest() {
  const InteractionState state = state_machine_.snapshot().state;
  if (state != InteractionState::kIdle && state != InteractionState::kListening &&
      state != InteractionState::kFollowUp) {
    return false;
  }
  if (state == InteractionState::kFollowUp) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    follow_up_deadline_.reset();
  }
  return state_machine_.Handle(ConversationEvent::kSpeechSegmentReady,
                               "final speech segment received");
}

void VoiceInteractionService::BeginAssistantCall() {
  std::lock_guard<std::mutex> lock(provider_lifecycle_mutex_);
  assistant_call_active_ = true;
  assistant_cancel_requested_ = false;
}

void VoiceInteractionService::EndAssistantCall() {
  std::lock_guard<std::mutex> lock(provider_lifecycle_mutex_);
  assistant_call_active_ = false;
}

void VoiceInteractionService::CancelAssistantCall() {
  bool cancel = false;
  {
    std::lock_guard<std::mutex> lock(provider_lifecycle_mutex_);
    if (assistant_call_active_ && !assistant_cancel_requested_) {
      assistant_cancel_requested_ = true;
      cancel = true;
    }
  }
  if (cancel && assistant_ != nullptr) {
    assistant_->Cancel();
  }
}

std::shared_ptr<ActionCancellation> VoiceInteractionService::BeginActionCall() {
  std::lock_guard<std::mutex> lock(provider_lifecycle_mutex_);
  action_call_active_ = true;
  action_cancel_requested_ = false;
  active_action_cancellation_ = std::make_shared<ActionCancellation>();
  return active_action_cancellation_;
}

void VoiceInteractionService::EndActionCall(
    const std::shared_ptr<ActionCancellation>& cancellation) {
  std::lock_guard<std::mutex> lock(provider_lifecycle_mutex_);
  if (active_action_cancellation_ == cancellation) {
    action_call_active_ = false;
    active_action_cancellation_.reset();
  }
}

void VoiceInteractionService::CancelActionCall() {
  bool cancel = false;
  {
    std::lock_guard<std::mutex> lock(provider_lifecycle_mutex_);
    if (action_call_active_ && !action_cancel_requested_) {
      action_cancel_requested_ = true;
      if (active_action_cancellation_ != nullptr) {
        active_action_cancellation_->RequestCancellation();
      }
      cancel = true;
    }
  }
  if (cancel && dispatcher_ != nullptr) {
    dispatcher_->Cancel();
  }
}

void VoiceInteractionService::HandleOutputResult(std::uint64_t request_generation,
                                                 VoiceOutputResult result) {
  bool recovery = false;
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (result.request_id == 0U || result.request_id != active_output_request_id_ ||
        request_generation != active_output_generation_ ||
        request_generation != interrupt_generation_.load()) {
      return;
    }
    active_output_request_id_ = 0U;
    active_output_generation_ = 0U;
    recovery = active_output_recovery_;
    active_output_recovery_ = false;
  }

  if (recovery) {
    if (result.status == VoiceOutputStatus::kFailed ||
        result.status == VoiceOutputStatus::kDropped) {
      SetLastError(result.error.empty() ? "voice recovery playback failed" : result.error);
    }
    ReturnToIdle("voice recovery prompt finished");
    return;
  }

  switch (result.status) {
    case VoiceOutputStatus::kCompleted:
      if (state_machine_.Handle(ConversationEvent::kPlaybackCompleted,
                                "audio playback completed")) {
        std::lock_guard<std::mutex> lock(output_mutex_);
        if (request_generation == interrupt_generation_.load() &&
            state_machine_.snapshot().state == InteractionState::kFollowUp) {
          follow_up_deadline_ = std::chrono::steady_clock::now() + follow_up_window_;
        }
      }
      return;
    case VoiceOutputStatus::kCancelled:
      if (state_machine_.Handle(ConversationEvent::kCancelRequested, "audio playback cancelled")) {
        ReturnToIdle("voice playback cancellation completed");
      }
      return;
    case VoiceOutputStatus::kDropped:
    case VoiceOutputStatus::kFailed:
      processing_errors_.fetch_add(1U);
      SetLastError(result.error.empty() ? "voice playback failed" : result.error);
      RecoverFromError(result.error.empty() ? "voice playback failed" : result.error,
                       request_generation);
      return;
  }
}

void VoiceInteractionService::ExpireFollowUpIfNeeded() {
  std::uint64_t generation = 0U;
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (!follow_up_deadline_.has_value() ||
        std::chrono::steady_clock::now() < *follow_up_deadline_) {
      return;
    }
    follow_up_deadline_.reset();
    generation = interrupt_generation_.load();
  }
  if (generation == interrupt_generation_.load()) {
    state_machine_.Handle(ConversationEvent::kFollowUpExpired, "follow-up window expired");
  }
}

void VoiceInteractionService::InvalidateOutputLifecycle() {
  std::lock_guard<std::mutex> lock(output_mutex_);
  active_output_request_id_ = 0U;
  active_output_generation_ = 0U;
  active_output_recovery_ = false;
  follow_up_deadline_.reset();
}

void VoiceInteractionService::RecoverFromError(const std::string& reason,
                                               std::uint64_t request_generation,
                                               std::optional<VoiceResponse> response,
                                               bool play_prompt) {
  const InteractionState state = state_machine_.snapshot().state;
  if (state == InteractionState::kShuttingDown || state == InteractionState::kCancelled) {
    return;
  }
  transcript_events_.DiscardPending();
  if (state_machine_.Handle(ConversationEvent::kFailure, reason)) {
    if (play_prompt && output_ != nullptr && request_generation == interrupt_generation_.load()) {
      VoiceResponse recovery = response.value_or(VoiceResponse{});
      recovery.response_text = kRecoveryPrompt;
      recovery = PublishResponse(std::move(recovery));
      SetLastError(reason);
      if (SubmitOutput(std::move(recovery), request_generation, true)) {
        return;
      }
      SetLastError("voice recovery prompt was rejected");
    }
    ReturnToIdle("voice error recovery completed");
  }
}

bool VoiceInteractionService::SubmitOutput(VoiceResponse response, std::uint64_t request_generation,
                                           bool recovery) {
  if (output_ == nullptr || response.response_text.empty()) {
    ReturnToIdle(recovery ? "voice recovery completed without playback"
                          : "voice request completed without playback");
    return true;
  }
  if (!recovery && !state_machine_.Handle(ConversationEvent::kResponseReady,
                                          "response submitted for playback")) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    active_output_request_id_ = response.id;
    active_output_generation_ = request_generation;
    active_output_recovery_ = recovery;
    follow_up_deadline_.reset();
  }
  if (output_->Submit(response.id, response.response_text,
                      [this, request_generation](VoiceOutputResult output_result) {
                        HandleOutputResult(request_generation, std::move(output_result));
                      })) {
    return true;
  }
  std::lock_guard<std::mutex> lock(output_mutex_);
  if (active_output_request_id_ == response.id) {
    active_output_request_id_ = 0U;
    active_output_generation_ = 0U;
    active_output_recovery_ = false;
  }
  return false;
}

void VoiceInteractionService::ReturnToIdle(const std::string& reason) {
  const InteractionState state = state_machine_.snapshot().state;
  if (state == InteractionState::kCancelled || state == InteractionState::kErrorRecovery) {
    state_machine_.Handle(ConversationEvent::kRecoveryCompleted, reason);
  } else if (state != InteractionState::kIdle && state != InteractionState::kShuttingDown) {
    state_machine_.Handle(ConversationEvent::kRequestCompleted, reason);
  }
}

void VoiceInteractionService::RecordUpstreamReconnect() {
  upstream_reconnects_.fetch_add(1U);
}

void VoiceInteractionService::SetLastError(std::string error) {
  std::lock_guard<std::mutex> lock(response_mutex_);
  last_error_ = std::move(error);
}

VoiceResponse VoiceInteractionService::PublishResponse(VoiceResponse response) {
  {
    std::lock_guard<std::mutex> lock(response_mutex_);
    response.id = ++response_version_;
    response_history_.push_back(response);
    while (response_history_.size() > 32U) {
      response_history_.pop_front();
    }
    last_error_.clear();
  }
  responses_published_.fetch_add(1U);
  response_changed_.notify_all();
  if (response_observer_) {
    response_observer_(response);
  }
  return response;
}

void VoiceInteractionService::ProcessLoop() {
  while (worker_running_.load()) {
    auto transcript = transcript_events_.WaitPopFor(std::chrono::milliseconds(100));
    if (!transcript.has_value()) {
      ExpireFollowUpIfNeeded();
      continue;
    }
    HandleTranscript(*transcript);
  }
}

}  // namespace voice
}  // namespace cockpit
