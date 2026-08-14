#include "agent/interaction/voice_interaction_service.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "cockpit/modules/voice/actions/cockpit_action_dispatcher.h"
#include "cockpit/modules/voice/actions/mock_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

namespace {

class CountingResponseSink final : public cockpit::voice::VoiceResponseSink {
 public:
  bool Submit(std::uint64_t request_id, std::string,
              cockpit::voice::VoiceOutputCompletion completion) override {
    ++submitted_;
    completion({request_id, cockpit::voice::VoiceOutputStatus::kCompleted, {}});
    return true;
  }

  cockpit::voice::VoiceOutputMetrics metrics() const override {
    cockpit::voice::VoiceOutputMetrics result;
    result.queued = submitted_;
    return result;
  }

 private:
  std::uint64_t submitted_ = 0;
};

class BlockingActionDispatcher final : public cockpit::voice::ActionDispatcher {
 public:
  cockpit::voice::ActionExecutionResult Execute(
      cockpit::voice::VoiceAction, const cockpit::voice::ActionExecutionContext& context) override {
    std::unique_lock<std::mutex> lock(mutex_);
    deadline_propagated_ = context.deadline > std::chrono::steady_clock::now();
    entered_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] {
      return cancelled_;
    });
    return {cockpit::voice::ActionExecutionStatus::kFailed, "cancelled"};
  }

  void Cancel() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancelled_ = true;
      ++cancel_calls_;
    }
    changed_.notify_all();
  }

  std::uint64_t cancel_calls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancel_calls_;
  }

  bool deadline_propagated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deadline_propagated_;
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
                                 return entered_;
                               });
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool cancelled_ = false;
  bool deadline_propagated_ = false;
  std::uint64_t cancel_calls_ = 0U;
};

class CountingVehicleStatusProvider final : public cockpit::voice::VehicleStatusProvider {
 public:
  bool GetLatest(const cockpit::voice::ActionExecutionContext&,
                 cockpit::voice::VehicleStatusSnapshot* status, std::string*) override {
    call_count_.fetch_add(1U);
    if (status != nullptr) {
      status->source = "pre-dispatch-test";
    }
    return true;
  }

  std::uint64_t call_count() const {
    return call_count_.load();
  }

 private:
  std::atomic<std::uint64_t> call_count_{0U};
};

class PredispatchGateDispatcher final : public cockpit::voice::ActionDispatcher {
 public:
  PredispatchGateDispatcher() {
    auto provider = std::make_unique<CountingVehicleStatusProvider>();
    provider_observer_ = provider.get();
    dispatcher_ = std::make_unique<cockpit::voice::CockpitActionDispatcher>(std::move(provider));
  }

  cockpit::voice::ActionExecutionResult Execute(
      cockpit::voice::VoiceAction action,
      const cockpit::voice::ActionExecutionContext& context) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      entered_ = true;
      changed_.notify_all();
      changed_.wait(lock, [this] {
        return released_;
      });
    }
    const auto result = dispatcher_->Execute(action, context);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      finished_ = true;
    }
    changed_.notify_all();
    return result;
  }

  void Cancel() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++cancel_calls_;
    }
    dispatcher_->Cancel();
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
                                 return entered_;
                               });
  }

  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

  bool WaitUntilFinished() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
                                 return finished_;
                               });
  }

  std::uint64_t side_effects() const {
    return provider_observer_->call_count();
  }

  std::uint64_t cancel_calls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancel_calls_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
  bool finished_ = false;
  std::uint64_t cancel_calls_ = 0U;
  CountingVehicleStatusProvider* provider_observer_ = nullptr;
  std::unique_ptr<cockpit::voice::CockpitActionDispatcher> dispatcher_;
};

class RecoveringVoiceAssistant final : public cockpit::voice::VoiceAssistant {
 public:
  cockpit::voice::VoiceAssistantResult HandleTranscript(
      const cockpit::voice::SpeechTranscript&, std::chrono::steady_clock::time_point) override {
    ++calls_;
    if (calls_ == 1) {
      std::unique_lock<std::mutex> lock(mutex_);
      cancelled_ = false;
      changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1), [this] {
        return cancelled_;
      });
      throw std::runtime_error("provider cancelled at deadline");
    } else if (calls_ == 2) {
      throw std::runtime_error("mock provider failed");
    }
    return {cockpit::voice::VoiceIntent::kUnknown, cockpit::voice::VoiceAction::kNone,
            "provider recovered"};
  }

  void Cancel() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancelled_ = true;
      ++cancel_calls_;
    }
    changed_.notify_all();
  }

  std::uint64_t cancel_calls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancel_calls_;
  }

 private:
  int calls_ = 0;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  bool cancelled_ = false;
  std::uint64_t cancel_calls_ = 0U;
};

class ManualResponseSink final : public cockpit::voice::VoiceResponseSink {
 public:
  bool Submit(std::uint64_t request_id, std::string text,
              cockpit::voice::VoiceOutputCompletion completion) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (reject_) {
        return false;
      }
      callbacks_[request_id] = std::move(completion);
      texts_[request_id] = std::move(text);
      active_request_id_ = request_id;
      ++submissions_;
    }
    changed_.notify_all();
    return true;
  }

  cockpit::voice::VoiceOutputMetrics metrics() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    cockpit::voice::VoiceOutputMetrics result;
    result.queued = submissions_;
    return result;
  }

  void Interrupt() override {
    cockpit::voice::VoiceOutputCompletion completion;
    std::uint64_t request_id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++interruptions_;
      request_id = active_request_id_;
      const auto found = callbacks_.find(request_id);
      if (found != callbacks_.end()) {
        completion = found->second;
      }
      active_request_id_ = 0U;
    }
    if (completion) {
      completion(
          {request_id, cockpit::voice::VoiceOutputStatus::kCancelled, "manual playback cancelled"});
    }
  }

  void Stop() override {
    Interrupt();
  }

  bool WaitForSubmissions(std::uint64_t count) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this, count] {
                                 return submissions_ >= count;
                               });
  }

  void Complete(std::uint64_t request_id, cockpit::voice::VoiceOutputStatus status,
                std::string error = {}) {
    cockpit::voice::VoiceOutputCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto found = callbacks_.find(request_id);
      if (found != callbacks_.end()) {
        completion = found->second;
      }
      if (active_request_id_ == request_id) {
        active_request_id_ = 0U;
      }
    }
    if (completion) {
      completion({request_id, status, std::move(error)});
    }
  }

  void set_reject(bool reject) {
    std::lock_guard<std::mutex> lock(mutex_);
    reject_ = reject;
  }

  std::uint64_t interruptions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return interruptions_;
  }

  std::string text(std::uint64_t request_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = texts_.find(request_id);
    return found == texts_.end() ? std::string{} : found->second;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::map<std::uint64_t, cockpit::voice::VoiceOutputCompletion> callbacks_;
  std::map<std::uint64_t, std::string> texts_;
  std::uint64_t active_request_id_ = 0;
  std::uint64_t submissions_ = 0;
  std::uint64_t interruptions_ = 0;
  bool reject_ = false;
};

bool WaitForState(cockpit::voice::VoiceInteractionService& service,
                  cockpit::voice::InteractionState expected,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (service.status().state == expected) {
      return true;
    }
    std::this_thread::yield();
  }
  return service.status().state == expected;
}

}  // namespace

int main() {
  cockpit::voice::VoiceInteractionService disabled(false, nullptr, nullptr);
  cockpit::voice::SpeechTranscript transcript;
  transcript.text = "open camera";
  if (disabled.HandleTranscript(transcript).has_value() ||
      disabled.status().state != cockpit::voice::InteractionState::kDisabled) {
    std::cerr << "disabled voice service accepted a transcript\n";
    return 1;
  }

  auto sink = std::make_unique<CountingResponseSink>();
  cockpit::voice::VoiceInteractionService service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::make_unique<cockpit::voice::MockActionDispatcher>(), std::move(sink));
  transcript.id = 10;
  auto first = service.HandleTranscript(transcript);
  transcript.id = 11;
  transcript.text = "something unknown";
  auto second = service.HandleTranscript(transcript);
  if (!first.has_value() || !second.has_value() || first->id >= second->id ||
      first->action != cockpit::voice::VoiceAction::kOpenCamera ||
      first->action_status != cockpit::voice::ActionExecutionStatus::kSucceeded ||
      second->action != cockpit::voice::VoiceAction::kNone) {
    std::cerr << "voice response generation failed\n";
    return 1;
  }

  cockpit::voice::VoiceResponse waited;
  if (!service.WaitForResponse(first->id, std::chrono::milliseconds(10), &waited) ||
      waited.id != second->id) {
    std::cerr << "voice response history is not ordered\n";
    return 1;
  }
  const auto status = service.status();
  if (status.state != cockpit::voice::InteractionState::kFollowUp ||
      status.metrics.transcripts_received != 2 || status.metrics.transcript_events_dropped != 0 ||
      status.metrics.responses_published != 2 || status.metrics.unknown_intents != 1 ||
      status.metrics.actions_attempted != 1 || status.metrics.actions_succeeded != 1 ||
      status.metrics.actions_failed != 0 || status.metrics.output.queued != 2 ||
      !status.latest_response.has_value() || status.latest_response->id != second->id) {
    std::cerr << "voice interaction metrics are invalid\n";
    return 1;
  }

  cockpit::voice::VoiceInteractionService no_dispatcher(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), nullptr);
  transcript.text = "open camera";
  const auto unavailable = no_dispatcher.HandleTranscript(transcript);
  if (!unavailable.has_value() ||
      unavailable->action_status != cockpit::voice::ActionExecutionStatus::kNotImplemented ||
      no_dispatcher.status().metrics.actions_failed != 1) {
    std::cerr << "missing dispatcher was not reported\n";
    return 1;
  }

  cockpit::voice::SpeechTranscript empty;
  if (service.HandleTranscript(empty).has_value() ||
      service.status().metrics.processing_errors != 1) {
    std::cerr << "empty transcript error was not recorded\n";
    return 1;
  }

  cockpit::voice::VoiceInteractionService recovering_service(
      true, std::make_unique<RecoveringVoiceAssistant>(), nullptr, nullptr, nullptr,
      std::chrono::milliseconds(10));
  cockpit::voice::SpeechTranscript provider_transcript;
  provider_transcript.text = "provider test";
  const auto timeout_started = std::chrono::steady_clock::now();
  if (recovering_service.HandleTranscript(provider_transcript).has_value() ||
      std::chrono::steady_clock::now() - timeout_started > std::chrono::milliseconds(300) ||
      recovering_service.status().metrics.assistant_timeouts != 1 ||
      recovering_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "voice provider timeout was not recorded\n";
    return 1;
  }
  if (recovering_service.HandleTranscript(provider_transcript).has_value() ||
      recovering_service.status().metrics.assistant_failures != 1) {
    std::cerr << "voice provider failure was not recorded\n";
    return 1;
  }
  const auto recovered = recovering_service.HandleTranscript(provider_transcript);
  const auto recovered_status = recovering_service.status();
  if (!recovered.has_value() || recovered_status.metrics.processing_errors != 2 ||
      recovered_status.metrics.responses_published != 1 || !recovered_status.last_error.empty() ||
      recovered_status.state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "voice service did not recover after provider failure\n";
    return 1;
  }

  auto recovery_assistant = std::make_unique<RecoveringVoiceAssistant>();
  auto* recovery_assistant_observer = recovery_assistant.get();
  auto recovery_sink = std::make_unique<ManualResponseSink>();
  auto* recovery_output = recovery_sink.get();
  cockpit::voice::VoiceInteractionService recovery_prompt_service(
      true, std::move(recovery_assistant), nullptr, std::move(recovery_sink), nullptr,
      std::chrono::milliseconds(10), std::chrono::seconds(1), std::chrono::seconds(1));
  if (recovery_prompt_service.HandleTranscript(provider_transcript).has_value()) {
    std::cerr << "assistant timeout unexpectedly returned a normal response\n";
    return 1;
  }
  const auto timeout_prompt = recovery_prompt_service.status().latest_response;
  if (!timeout_prompt.has_value() ||
      recovery_prompt_service.status().state != cockpit::voice::InteractionState::kErrorRecovery ||
      recovery_prompt_service.status().metrics.assistant_timeouts != 1U ||
      recovery_assistant_observer->cancel_calls() != 1U ||
      recovery_output->text(timeout_prompt->id) !=
          "Sorry, I couldn't complete that request. Please try again.") {
    std::cerr << "assistant timeout did not enter fixed-prompt recovery\n";
    return 1;
  }
  recovery_output->Complete(timeout_prompt->id, cockpit::voice::VoiceOutputStatus::kCompleted);
  if (recovery_prompt_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "completed recovery prompt did not return to Idle\n";
    return 1;
  }

  if (recovery_prompt_service.HandleTranscript(provider_transcript).has_value()) {
    std::cerr << "assistant failure unexpectedly returned a normal response\n";
    return 1;
  }
  const auto failure_prompt = recovery_prompt_service.status().latest_response;
  if (!failure_prompt.has_value() ||
      recovery_prompt_service.status().metrics.assistant_failures != 1U ||
      recovery_prompt_service.status().state != cockpit::voice::InteractionState::kErrorRecovery) {
    std::cerr << "assistant failure did not keep recovery active\n";
    return 1;
  }
  const auto recovery_interrupt = recovery_prompt_service.Interrupt();
  if (!recovery_interrupt.active_request_interrupted ||
      recovery_prompt_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "interrupt did not cancel the recovery prompt\n";
    return 1;
  }
  recovery_output->Complete(failure_prompt->id, cockpit::voice::VoiceOutputStatus::kCompleted);
  if (recovery_prompt_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "stale recovery prompt callback changed state\n";
    return 1;
  }

  const auto normal_after_recovery = recovery_prompt_service.HandleTranscript(provider_transcript);
  if (!normal_after_recovery.has_value()) {
    std::cerr << "service did not accept a request after recovery\n";
    return 1;
  }
  const auto output_count_before_failure = recovery_prompt_service.status().metrics.output.queued;
  recovery_output->Complete(normal_after_recovery->id, cockpit::voice::VoiceOutputStatus::kFailed,
                            "speaker failed");
  if (recovery_prompt_service.status().state != cockpit::voice::InteractionState::kIdle ||
      recovery_prompt_service.status().metrics.output.queued != output_count_before_failure) {
    std::cerr << "playback failure recursively created a recovery prompt\n";
    return 1;
  }

  auto lifecycle_sink = std::make_unique<ManualResponseSink>();
  auto* lifecycle_output = lifecycle_sink.get();
  cockpit::voice::VoiceInteractionService lifecycle_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::make_unique<cockpit::voice::MockActionDispatcher>(), std::move(lifecycle_sink), nullptr,
      std::chrono::seconds(1), std::chrono::seconds(1), std::chrono::milliseconds(30));
  if (!lifecycle_service.Start()) {
    std::cerr << "playback lifecycle service did not start\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript lifecycle_transcript;
  lifecycle_transcript.id = 400;
  lifecycle_transcript.text = "something unknown";
  if (lifecycle_service.SubmitTranscript(lifecycle_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !lifecycle_output->WaitForSubmissions(1U) ||
      lifecycle_service.status().state != cockpit::voice::InteractionState::kSpeaking) {
    std::cerr << "accepted output did not keep the interaction in Speaking\n";
    return 1;
  }
  const auto first_lifecycle_response = lifecycle_service.status().latest_response;
  if (!first_lifecycle_response.has_value()) {
    std::cerr << "playback lifecycle response was not published\n";
    return 1;
  }
  lifecycle_output->Complete(first_lifecycle_response->id,
                             cockpit::voice::VoiceOutputStatus::kCompleted);
  if (lifecycle_service.status().state != cockpit::voice::InteractionState::kFollowUp ||
      !WaitForState(lifecycle_service, cockpit::voice::InteractionState::kIdle)) {
    std::cerr << "real playback completion did not enter and expire FollowUp\n";
    return 1;
  }

  lifecycle_transcript.id = 401;
  if (lifecycle_service.SubmitTranscript(lifecycle_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !lifecycle_output->WaitForSubmissions(2U)) {
    std::cerr << "second playback lifecycle request was not accepted\n";
    return 1;
  }
  const auto interrupted_response = lifecycle_service.status().latest_response;
  if (!interrupted_response.has_value() ||
      lifecycle_service.status().state != cockpit::voice::InteractionState::kSpeaking) {
    std::cerr << "second playback did not enter Speaking\n";
    return 1;
  }
  lifecycle_output->Complete(interrupted_response->id,
                             cockpit::voice::VoiceOutputStatus::kCompleted);
  lifecycle_transcript.id = 402;
  if (lifecycle_service.status().state != cockpit::voice::InteractionState::kFollowUp ||
      lifecycle_service.SubmitTranscript(lifecycle_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !lifecycle_output->WaitForSubmissions(3U) ||
      lifecycle_service.status().state != cockpit::voice::InteractionState::kSpeaking) {
    std::cerr << "transcript inside FollowUp did not start a new request\n";
    return 1;
  }
  const auto interrupted_playback = lifecycle_service.status().latest_response;
  const auto playback_interrupt = lifecycle_service.Interrupt();
  if (!playback_interrupt.active_request_interrupted || lifecycle_output->interruptions() != 1U ||
      lifecycle_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "interrupt during Speaking did not stop voice output\n";
    return 1;
  }

  lifecycle_transcript.id = 403;
  if (lifecycle_service.SubmitTranscript(lifecycle_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !lifecycle_output->WaitForSubmissions(4U)) {
    std::cerr << "voice service did not accept work after playback interruption\n";
    return 1;
  }
  const auto current_response = lifecycle_service.status().latest_response;
  if (!interrupted_playback.has_value()) {
    std::cerr << "interrupted playback response was not published\n";
    return 1;
  }
  lifecycle_output->Complete(interrupted_playback->id,
                             cockpit::voice::VoiceOutputStatus::kCompleted);
  if (!current_response.has_value() ||
      lifecycle_service.status().state != cockpit::voice::InteractionState::kSpeaking) {
    std::cerr << "stale playback completion changed the new conversation\n";
    return 1;
  }
  lifecycle_output->Complete(current_response->id, cockpit::voice::VoiceOutputStatus::kFailed,
                             "speaker failed");
  if (!WaitForState(lifecycle_service, cockpit::voice::InteractionState::kIdle) ||
      lifecycle_service.status().metrics.processing_errors == 0U) {
    std::cerr << "playback failure did not enter recovery\n";
    return 1;
  }
  lifecycle_service.Stop();

  auto timeout_dispatcher = std::make_unique<BlockingActionDispatcher>();
  auto* timeout_dispatcher_observer = timeout_dispatcher.get();
  auto action_timeout_sink = std::make_unique<ManualResponseSink>();
  auto* action_timeout_output = action_timeout_sink.get();
  cockpit::voice::VoiceInteractionService action_timeout_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), std::move(timeout_dispatcher),
      std::move(action_timeout_sink), nullptr, std::chrono::seconds(1),
      std::chrono::milliseconds(10), std::chrono::seconds(1));
  cockpit::voice::SpeechTranscript action_timeout_transcript;
  action_timeout_transcript.id = 450U;
  action_timeout_transcript.text = "show vehicle status";
  const auto action_started = std::chrono::steady_clock::now();
  if (action_timeout_service.HandleTranscript(action_timeout_transcript).has_value() ||
      std::chrono::steady_clock::now() - action_started > std::chrono::milliseconds(300)) {
    std::cerr << "action execution deadline was not bounded\n";
    return 1;
  }
  const auto action_timeout_prompt = action_timeout_service.status().latest_response;
  if (!action_timeout_prompt.has_value() || !timeout_dispatcher_observer->deadline_propagated() ||
      timeout_dispatcher_observer->cancel_calls() != 1U ||
      action_timeout_service.status().metrics.action_timeouts != 1U ||
      action_timeout_service.status().state != cockpit::voice::InteractionState::kErrorRecovery) {
    std::cerr << "action timeout did not cancel once and enter recovery\n";
    return 1;
  }
  action_timeout_output->Complete(action_timeout_prompt->id,
                                  cockpit::voice::VoiceOutputStatus::kCompleted);
  if (action_timeout_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "action timeout recovery did not return to Idle\n";
    return 1;
  }

  auto rejecting_sink = std::make_unique<ManualResponseSink>();
  auto* rejecting_output = rejecting_sink.get();
  rejecting_output->set_reject(true);
  cockpit::voice::VoiceInteractionService rejecting_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), nullptr,
      std::move(rejecting_sink));
  if (rejecting_service.HandleTranscript(lifecycle_transcript).has_value() ||
      rejecting_service.status().state != cockpit::voice::InteractionState::kIdle ||
      rejecting_service.status().metrics.processing_errors == 0U) {
    std::cerr << "rejected playback queue produced a successful response\n";
    return 1;
  }

  cockpit::voice::VoiceInteractionService async_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::make_unique<cockpit::voice::MockActionDispatcher>());
  if (!async_service.Start()) {
    std::cerr << "async voice service did not start\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript async_transcript;
  async_transcript.id = 99;
  async_transcript.text = "show vehicle status";
  for (std::uint64_t id = 99; id <= 101; ++id) {
    async_transcript.id = id;
    if (async_service.SubmitTranscript(async_transcript) !=
        cockpit::event::EventQueuePushResult::kAccepted) {
      std::cerr << "async voice service rejected consecutive transcript\n";
      return 1;
    }
  }
  cockpit::voice::SpeechTranscript observed_transcript;
  if (!async_service.WaitForTranscript(99, std::chrono::milliseconds(10), &observed_transcript) ||
      observed_transcript.id != 100) {
    std::cerr << "agent transcript history is not ordered\n";
    return 1;
  }
  std::uint64_t after_id = 0;
  for (std::uint64_t transcript_id = 99; transcript_id <= 101; ++transcript_id) {
    cockpit::voice::VoiceResponse async_response;
    if (!async_service.WaitForResponse(after_id, std::chrono::milliseconds(500), &async_response) ||
        async_response.transcript_id != transcript_id) {
      std::cerr << "consecutive voice responses are out of order\n";
      return 1;
    }
    after_id = async_response.id;
  }
  async_service.Stop();
  if (async_service.SubmitTranscript(async_transcript) !=
          cockpit::event::EventQueuePushResult::kClosed ||
      async_service.status().state != cockpit::voice::InteractionState::kShuttingDown) {
    std::cerr << "stopped async voice service accepted transcript\n";
    return 1;
  }

  auto interrupt_dispatcher = std::make_unique<BlockingActionDispatcher>();
  auto* interrupt_observer = interrupt_dispatcher.get();
  cockpit::voice::VoiceInteractionService interrupt_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::move(interrupt_dispatcher));
  if (!interrupt_service.Start()) {
    std::cerr << "interruptible voice service did not start\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript interrupt_transcript;
  interrupt_transcript.id = 200;
  interrupt_transcript.text = "show vehicle status";
  if (interrupt_service.SubmitTranscript(interrupt_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !interrupt_observer->WaitUntilEntered() ||
      interrupt_service.status().state != cockpit::voice::InteractionState::kExecuting) {
    std::cerr << "interruptible action did not start\n";
    return 1;
  }
  interrupt_transcript.id = 201;
  interrupt_service.SubmitTranscript(interrupt_transcript);
  interrupt_transcript.id = 202;
  interrupt_service.SubmitTranscript(interrupt_transcript);
  const auto interrupt_result = interrupt_service.Interrupt();
  interrupt_transcript.id = 203;
  interrupt_transcript.text = "something unknown";
  if (!interrupt_result.active_request_interrupted ||
      interrupt_result.queued_transcripts_discarded != 2 ||
      interrupt_service.SubmitTranscript(interrupt_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted) {
    std::cerr << "voice interrupt did not cancel active and queued work\n";
    return 1;
  }
  cockpit::voice::VoiceResponse interrupt_recovery;
  if (!interrupt_service.WaitForResponse(0, std::chrono::milliseconds(500), &interrupt_recovery) ||
      interrupt_recovery.transcript_id != 203 ||
      interrupt_service.status().metrics.requests_interrupted != 1 ||
      interrupt_service.status().metrics.transcript_events_dropped < 2) {
    std::cerr << "voice service did not recover after interruption\n";
    return 1;
  }
  if (interrupt_service.status().state != cockpit::voice::InteractionState::kIdle ||
      interrupt_service.status().metrics.state_transitions == 0) {
    std::cerr << "voice state machine did not recover after interruption\n";
    return 1;
  }
  interrupt_service.Stop();

  auto predispatch_dispatcher = std::make_unique<PredispatchGateDispatcher>();
  auto* predispatch_observer = predispatch_dispatcher.get();
  cockpit::voice::VoiceInteractionService predispatch_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::move(predispatch_dispatcher));
  if (!predispatch_service.Start()) {
    std::cerr << "pre-dispatch cancellation service did not start\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript predispatch_transcript;
  predispatch_transcript.id = 250U;
  predispatch_transcript.text = "show vehicle status";
  if (predispatch_service.SubmitTranscript(predispatch_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !predispatch_observer->WaitUntilEntered()) {
    std::cerr << "action did not enter the pre-dispatch cancellation window\n";
    return 1;
  }
  const auto predispatch_interrupt = predispatch_service.Interrupt();
  predispatch_observer->Release();
  if (!predispatch_observer->WaitUntilFinished() ||
      !predispatch_interrupt.active_request_interrupted ||
      predispatch_observer->cancel_calls() != 1U || predispatch_observer->side_effects() != 0U ||
      predispatch_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "interrupt between action selection and provider dispatch caused a side effect\n";
    return 1;
  }
  predispatch_service.Stop();

  auto blocking_dispatcher = std::make_unique<BlockingActionDispatcher>();
  auto* blocking_observer = blocking_dispatcher.get();
  cockpit::voice::VoiceInteractionService cancellable_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), std::move(blocking_dispatcher));
  if (!cancellable_service.Start()) {
    std::cerr << "cancellable voice service did not start\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript blocking_transcript;
  blocking_transcript.id = 300;
  blocking_transcript.text = "show vehicle status";
  if (cancellable_service.SubmitTranscript(blocking_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !blocking_observer->WaitUntilEntered()) {
    std::cerr << "blocking action did not start\n";
    return 1;
  }
  blocking_transcript.id = 301;
  cancellable_service.SubmitTranscript(blocking_transcript);
  blocking_transcript.id = 302;
  cancellable_service.SubmitTranscript(blocking_transcript);
  const auto stop_started = std::chrono::steady_clock::now();
  cancellable_service.Stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  if (stop_elapsed > std::chrono::milliseconds(300) ||
      cancellable_service.status().metrics.transcript_events_dropped < 2 ||
      cancellable_service.status().metrics.responses_published != 0) {
    std::cerr << "voice stop did not cancel active work and discard queued transcripts\n";
    return 1;
  }
  std::cout << "voice interaction service tests passed\n";
  return 0;
}
