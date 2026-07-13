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

#include "cockpit/core/base/macros.h"
#include "cockpit/core/event/event_queue.h"
#include "cockpit/modules/voice/actions/action_dispatcher.h"
#include "cockpit/modules/voice/assistant/speech_transcript.h"
#include "cockpit/modules/voice/assistant/voice_assistant.h"
#include "cockpit/modules/voice/responses/voice_response_sink.h"

namespace cockpit {
namespace voice {

enum class InteractionState {
  kDisabled,
  kListening,
  kProcessing,
  kFaulted,
};

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
  VoiceOutputMetrics output;
};

struct VoiceInteractionStatus {
  InteractionState state = InteractionState::kDisabled;
  VoiceInteractionMetrics metrics;
  std::optional<VoiceResponse> latest_response;
  std::string last_error;
};

class VoiceInteractionService {
 public:
  using ResponseObserver = std::function<void(const VoiceResponse&)>;

  VoiceInteractionService(bool enabled, std::unique_ptr<VoiceAssistant> assistant,
                          std::unique_ptr<ActionDispatcher> dispatcher,
                          std::unique_ptr<VoiceResponseSink> output = nullptr,
                          ResponseObserver response_observer = nullptr);
  ~VoiceInteractionService();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(VoiceInteractionService);

  bool Start();
  void Stop();
  event::EventQueuePushResult SubmitTranscript(const SpeechTranscript& transcript);
  std::optional<VoiceResponse> HandleTranscript(const SpeechTranscript& transcript);
  bool WaitForResponse(std::uint64_t after_id, std::chrono::milliseconds timeout,
                       VoiceResponse* response) const;
  VoiceInteractionStatus status() const;
  void RecordUpstreamReconnect();
  void SetUpstreamError(std::string error);

 private:
  VoiceResponse PublishResponse(VoiceResponse response);
  void ProcessLoop();

  const bool enabled_;
  const std::unique_ptr<VoiceAssistant> assistant_;
  const std::unique_ptr<ActionDispatcher> dispatcher_;
  const std::unique_ptr<VoiceResponseSink> output_;
  const ResponseObserver response_observer_;
  mutable std::mutex processing_mutex_;
  event::EventQueue<SpeechTranscript> transcript_events_{32};
  std::atomic<bool> worker_running_{false};
  std::unique_ptr<std::thread> worker_;
  std::atomic<InteractionState> state_{InteractionState::kDisabled};
  std::atomic<std::uint64_t> transcripts_received_{0};
  std::atomic<std::uint64_t> responses_published_{0};
  std::atomic<std::uint64_t> unknown_intents_{0};
  std::atomic<std::uint64_t> processing_errors_{0};
  std::atomic<std::uint64_t> upstream_reconnects_{0};
  std::atomic<std::uint64_t> actions_attempted_{0};
  std::atomic<std::uint64_t> actions_succeeded_{0};
  std::atomic<std::uint64_t> actions_failed_{0};
  mutable std::mutex response_mutex_;
  mutable std::condition_variable response_changed_;
  std::deque<VoiceResponse> response_history_;
  std::uint64_t response_version_ = 0;
  std::string last_error_;
};

}  // namespace voice
}  // namespace cockpit
