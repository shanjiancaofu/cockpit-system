#include "voice_interaction_service.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace cockpit {
namespace voice {

VoiceInteractionService::VoiceInteractionService(
    bool enabled, std::unique_ptr<VoiceAssistant> assistant)
    : enabled_(enabled), assistant_(std::move(assistant)) {
  state_.store(enabled_ ? InteractionState::kListening
                        : InteractionState::kDisabled);
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
    response.timestamp_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
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
    response = PublishResponse(std::move(response));
    state_.store(InteractionState::kListening);
    return response;
  } catch (const std::exception& exception) {
    processing_errors_.fetch_add(1U);
    SetUpstreamError(exception.what());
    state_.store(InteractionState::kFaulted);
    return std::nullopt;
  }
}

bool VoiceInteractionService::WaitForResponse(
    std::uint64_t after_id, std::chrono::milliseconds timeout,
    VoiceResponse* response) const {
  std::unique_lock<std::mutex> lock(response_mutex_);
  response_changed_.wait_for(lock, timeout, [this, after_id] {
    return !response_history_.empty() && response_history_.back().id > after_id;
  });
  const auto next = std::find_if(
      response_history_.begin(), response_history_.end(),
      [after_id](const VoiceResponse& value) { return value.id > after_id; });
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
  result.metrics.responses_published = responses_published_.load();
  result.metrics.unknown_intents = unknown_intents_.load();
  result.metrics.processing_errors = processing_errors_.load();
  result.metrics.upstream_reconnects = upstream_reconnects_.load();
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
  return response;
}

}  // namespace voice
}  // namespace cockpit
