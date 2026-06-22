#include "modules/voice/mock_action_dispatcher.h"
#include "modules/voice/mock_voice_assistant.h"
#include "services/voice-interaction-service/voice_interaction_service.h"

#include <chrono>
#include <iostream>
#include <memory>

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
      std::make_unique<cockpit::voice::MockActionDispatcher>(),
      std::move(sink));
  transcript.id = 10;
  auto first = service.HandleTranscript(transcript);
  transcript.id = 11;
  transcript.text = "something unknown";
  auto second = service.HandleTranscript(transcript);
  if (!first.has_value() || !second.has_value() || first->id >= second->id ||
      first->action != cockpit::voice::VoiceAction::kOpenCamera ||
      first->action_status !=
          cockpit::voice::ActionExecutionStatus::kSucceeded ||
      second->action != cockpit::voice::VoiceAction::kNone) {
    std::cerr << "voice response generation failed\n";
    return 1;
  }

  cockpit::voice::VoiceResponse waited;
  if (!service.WaitForResponse(first->id, std::chrono::milliseconds(10),
                               &waited) ||
      waited.id != second->id) {
    std::cerr << "voice response history is not ordered\n";
    return 1;
  }
  const auto status = service.status();
  if (status.state != cockpit::voice::InteractionState::kListening ||
      status.metrics.transcripts_received != 2 ||
      status.metrics.responses_published != 2 ||
      status.metrics.unknown_intents != 1 ||
      status.metrics.actions_attempted != 1 ||
      status.metrics.actions_succeeded != 1 ||
      status.metrics.actions_failed != 0 ||
      status.metrics.output.queued != 2 ||
      !status.latest_response.has_value() ||
      status.latest_response->id != second->id) {
    std::cerr << "voice interaction metrics are invalid\n";
    return 1;
  }

  cockpit::voice::VoiceInteractionService no_dispatcher(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), nullptr);
  transcript.text = "start recording";
  const auto unavailable = no_dispatcher.HandleTranscript(transcript);
  if (!unavailable.has_value() ||
      unavailable->action_status !=
          cockpit::voice::ActionExecutionStatus::kNotImplemented ||
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
  std::cout << "voice interaction service tests passed\n";
  return 0;
}
