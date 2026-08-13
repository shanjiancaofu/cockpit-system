#include "agent/interaction/voice_interaction_service.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "cockpit/modules/voice/actions/mock_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

namespace {

class CountingResponseSink final : public cockpit::voice::VoiceResponseSink {
 public:
  bool Submit(std::string) override {
    ++submitted_;
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
  cockpit::voice::ActionExecutionResult Execute(cockpit::voice::VoiceAction) override {
    std::unique_lock<std::mutex> lock(mutex_);
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
    }
    changed_.notify_all();
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(1), [this] {
      return entered_;
    });
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool cancelled_ = false;
};

class RecoveringVoiceAssistant final : public cockpit::voice::VoiceAssistant {
 public:
  cockpit::voice::VoiceAssistantResult HandleTranscript(
      const cockpit::voice::SpeechTranscript&, std::chrono::steady_clock::time_point) override {
    ++calls_;
    if (calls_ == 1) {
      std::unique_lock<std::mutex> lock(mutex_);
      cancelled_ = false;
      changed_.wait_for(lock, std::chrono::seconds(1), [this] {
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
    }
    changed_.notify_all();
  }

 private:
  int calls_ = 0;
  std::mutex mutex_;
  std::condition_variable changed_;
  bool cancelled_ = false;
};

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
  if (status.state != cockpit::voice::InteractionState::kIdle ||
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
      recovering_service.status().metrics.provider_timeouts != 1 ||
      recovering_service.status().state != cockpit::voice::InteractionState::kIdle) {
    std::cerr << "voice provider timeout was not recorded\n";
    return 1;
  }
  if (recovering_service.HandleTranscript(provider_transcript).has_value() ||
      recovering_service.status().metrics.provider_failures != 1) {
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
