#include "voice_interaction_service.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace cockpit {
namespace voice {

VoiceInteractionService::VoiceInteractionService(bool enabled,
                                                 std::unique_ptr<VoiceAssistant> assistant,
                                                 std::unique_ptr<ActionDispatcher> dispatcher,
                                                 std::unique_ptr<VoiceResponseSink> output,
                                                 ResponseObserver response_observer)
    : enabled_(enabled),
      assistant_(std::move(assistant)),
      dispatcher_(std::move(dispatcher)),
      output_(std::move(output)),
      response_observer_(std::move(response_observer)) {
  state_.store(enabled_ ? InteractionState::kListening : InteractionState::kDisabled);
}

VoiceInteractionService::~VoiceInteractionService() {
  Stop();
}

bool VoiceInteractionService::Start() {
  if (!enabled_ || assistant_ == nullptr) {
    return false;
  }
  bool expected = false;
  if (!worker_running_.compare_exchange_strong(expected, true)) {
    return true;
  }
  transcript_events_.Reset();
  worker_ = std::make_unique<std::thread>(&VoiceInteractionService::ProcessLoop, this);
  state_.store(InteractionState::kListening);
  return true;
}

void VoiceInteractionService::Stop() {
  const bool was_running = worker_running_.exchange(false);
  transcript_events_.Close();
  if (worker_ != nullptr && worker_->joinable()) {
    worker_->join();
  }
  worker_.reset();
  if (was_running && enabled_) {
    state_.store(InteractionState::kListening);
  }
}

event::EventQueuePushResult VoiceInteractionService::SubmitTranscript(
    const SpeechTranscript& transcript) {
  if (!enabled_ || assistant_ == nullptr || !worker_running_.load()) {
    return event::EventQueuePushResult::kClosed;
  }
  return transcript_events_.Push(transcript);
}

std::optional<VoiceResponse> VoiceInteractionService::HandleTranscript(
    const SpeechTranscript& transcript) {
  if (!enabled_ || assistant_ == nullptr) {
    return std::nullopt;
  }
  std::lock_guard<std::mutex> processing_lock(processing_mutex_);
  if (transcript.text.empty()) {
    processing_errors_.fetch_add(1U);
    SetUpstreamError("transcript text is empty");
    return std::nullopt;
  }

  state_.store(InteractionState::kProcessing);
  transcripts_received_.fetch_add(1U);
  try {
    const VoiceAssistantResult result = assistant_->HandleTranscript(transcript);
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
      actions_attempted_.fetch_add(1U);
      if (dispatcher_ == nullptr) {
        response.action_status = ActionExecutionStatus::kNotImplemented;
        response.action_message = "No action dispatcher is configured.";
        response.response_text = response.action_message;
        actions_failed_.fetch_add(1U);
      } else {
        const ActionExecutionResult execution = dispatcher_->Execute(result.action);
        response.action_status = execution.status;
        response.action_message = execution.message;
        if (!execution.message.empty()) {
          response.response_text = execution.message;
        }
        if (execution.status == ActionExecutionStatus::kSucceeded) {
          actions_succeeded_.fetch_add(1U);
        } else {
          actions_failed_.fetch_add(1U);
        }
      }
    }
    response = PublishResponse(std::move(response));
    if (output_ != nullptr) {
      output_->Submit(response.response_text);
    }
    state_.store(InteractionState::kListening);
    return response;
  } catch (const std::exception& exception) {
    processing_errors_.fetch_add(1U);
    SetUpstreamError(exception.what());
    state_.store(InteractionState::kFaulted);
    return std::nullopt;
  }
}

bool VoiceInteractionService::WaitForResponse(std::uint64_t after_id,
                                              std::chrono::milliseconds timeout,
                                              VoiceResponse* response) const {
  std::unique_lock<std::mutex> lock(response_mutex_);
  response_changed_.wait_for(lock, timeout, [this, after_id] {
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

VoiceInteractionStatus VoiceInteractionService::status() const {
  VoiceInteractionStatus result;
  result.state = state_.load();
  result.metrics.transcripts_received = transcripts_received_.load();
  result.metrics.transcript_events_dropped = transcript_events_.DropCount();
  result.metrics.responses_published = responses_published_.load();
  result.metrics.unknown_intents = unknown_intents_.load();
  result.metrics.processing_errors = processing_errors_.load();
  result.metrics.upstream_reconnects = upstream_reconnects_.load();
  result.metrics.actions_attempted = actions_attempted_.load();
  result.metrics.actions_succeeded = actions_succeeded_.load();
  result.metrics.actions_failed = actions_failed_.load();
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

void VoiceInteractionService::RecordUpstreamReconnect() {
  upstream_reconnects_.fetch_add(1U);
}

void VoiceInteractionService::SetUpstreamError(std::string error) {
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
      continue;
    }
    HandleTranscript(*transcript);
  }

  while (true) {
    auto transcript = transcript_events_.TryPop();
    if (!transcript.has_value()) {
      break;
    }
    HandleTranscript(*transcript);
  }
}

}  // namespace voice
}  // namespace cockpit
