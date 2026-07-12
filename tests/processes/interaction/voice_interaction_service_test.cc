#include "cockpit/processes/interaction/interaction/voice_interaction_service.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>

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
  if (status.state != cockpit::voice::InteractionState::kListening ||
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
  if (async_service.SubmitTranscript(async_transcript) !=
      cockpit::event::EventQueuePushResult::kAccepted) {
    std::cerr << "async voice service rejected transcript\n";
    return 1;
  }
  cockpit::voice::VoiceResponse async_response;
  if (!async_service.WaitForResponse(0, std::chrono::milliseconds(500), &async_response) ||
      async_response.transcript_id != 99) {
    std::cerr << "async voice service did not publish response\n";
    return 1;
  }
  async_service.Stop();
  if (async_service.SubmitTranscript(async_transcript) !=
      cockpit::event::EventQueuePushResult::kClosed) {
    std::cerr << "stopped async voice service accepted transcript\n";
    return 1;
  }

  auto blocking_dispatcher = std::make_unique<BlockingActionDispatcher>();
  auto* blocking_observer = blocking_dispatcher.get();
  cockpit::voice::VoiceInteractionService cancellable_service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), std::move(blocking_dispatcher));
  if (!cancellable_service.Start()) {
    std::cerr << "cancellable voice service did not start\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript blocking_transcript;
  blocking_transcript.id = 200;
  blocking_transcript.text = "show vehicle status";
  if (cancellable_service.SubmitTranscript(blocking_transcript) !=
          cockpit::event::EventQueuePushResult::kAccepted ||
      !blocking_observer->WaitUntilEntered()) {
    std::cerr << "blocking action did not start\n";
    return 1;
  }
  blocking_transcript.id = 201;
  cancellable_service.SubmitTranscript(blocking_transcript);
  blocking_transcript.id = 202;
  cancellable_service.SubmitTranscript(blocking_transcript);
  const auto stop_started = std::chrono::steady_clock::now();
  cancellable_service.Stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  if (stop_elapsed > std::chrono::milliseconds(300) ||
      cancellable_service.status().metrics.transcript_events_dropped < 2) {
    std::cerr << "voice stop did not cancel active work and discard queued transcripts\n";
    return 1;
  }
  std::cout << "voice interaction service tests passed\n";
  return 0;
}
