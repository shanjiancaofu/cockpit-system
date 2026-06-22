#pragma once

#include "modules/voice/speech_transcript.h"
#include "modules/voice/voice_assistant.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

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
  std::string response_text;
};

struct VoiceInteractionMetrics {
  std::uint64_t transcripts_received = 0;
  std::uint64_t responses_published = 0;
  std::uint64_t unknown_intents = 0;
  std::uint64_t processing_errors = 0;
  std::uint64_t upstream_reconnects = 0;
};

struct VoiceInteractionStatus {
  InteractionState state = InteractionState::kDisabled;
  VoiceInteractionMetrics metrics;
  std::optional<VoiceResponse> latest_response;
  std::string last_error;
};

class VoiceInteractionService {
 public:
  VoiceInteractionService(bool enabled, std::unique_ptr<VoiceAssistant> assistant);

  VoiceInteractionService(const VoiceInteractionService&) = delete;
  VoiceInteractionService& operator=(const VoiceInteractionService&) = delete;

  std::optional<VoiceResponse> HandleTranscript(const SpeechTranscript& transcript);
  bool WaitForResponse(std::uint64_t after_id,
                       std::chrono::milliseconds timeout,
                       VoiceResponse* response) const;
  VoiceInteractionStatus status() const;
  void RecordUpstreamReconnect();
  void SetUpstreamError(std::string error);

 private:
  VoiceResponse PublishResponse(VoiceResponse response);

  const bool enabled_;
  const std::unique_ptr<VoiceAssistant> assistant_;
  mutable std::mutex processing_mutex_;
  std::atomic<InteractionState> state_{InteractionState::kDisabled};
  std::atomic<std::uint64_t> transcripts_received_{0};
  std::atomic<std::uint64_t> responses_published_{0};
  std::atomic<std::uint64_t> unknown_intents_{0};
  std::atomic<std::uint64_t> processing_errors_{0};
  std::atomic<std::uint64_t> upstream_reconnects_{0};
  mutable std::mutex response_mutex_;
  mutable std::condition_variable response_changed_;
  std::deque<VoiceResponse> response_history_;
  std::uint64_t response_version_ = 0;
  std::string last_error_;
};

}  // namespace voice
}  // namespace cockpit
