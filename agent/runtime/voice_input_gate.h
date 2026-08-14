#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include "agent/interaction/voice_interaction_service.h"
#include "agent/speech/kws/wake_prompt_player.h"
#include "agent/speech/kws/wake_word_detector.h"
#include "agent/speech/pipeline/speech_pipeline.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace agent {

enum class VoiceInputMode {
  kKws,
  kSpeech,
  kPaused,
};

struct VoiceInputGateMetrics {
  std::uint64_t kws_frames_processed = 0;
  std::uint64_t wake_detections = 0;
  std::uint64_t wake_detections_suppressed = 0;
  std::uint64_t speech_frames_forwarded = 0;
  std::uint64_t input_frames_paused = 0;
  std::uint64_t kws_errors = 0;
  std::uint64_t last_wake_timestamp_ms = 0;
};

class VoiceInputGate {
 public:
  VoiceInputGate(config::KwsConfig config, voice::VoiceInteractionService* service,
                 SpeechPipeline* speech_pipeline, std::unique_ptr<WakeWordDetector> detector,
                 std::unique_ptr<WakePromptPlayer> prompt_player);

  bool ProcessFrame(const audio::AudioFrame& frame);
  void Stop();
  VoiceInputGateMetrics metrics() const;
  VoiceInputMode mode() const;

 private:
  VoiceInputMode DetermineMode() const;
  void ResetSpeechInputIfLeavingSpeech(VoiceInputMode next_mode);
  bool AcceptWakeDetection();

  const config::KwsConfig config_;
  voice::VoiceInteractionService* service_;
  SpeechPipeline* speech_pipeline_;
  std::unique_ptr<WakeWordDetector> detector_;
  std::unique_ptr<WakePromptPlayer> prompt_player_;
  std::atomic_bool stopping_{false};
  VoiceInputMode last_mode_{VoiceInputMode::kPaused};
  std::chrono::steady_clock::time_point last_wake_;
  bool has_last_wake_ = false;
  std::atomic<std::uint64_t> kws_frames_processed_{0};
  std::atomic<std::uint64_t> wake_detections_{0};
  std::atomic<std::uint64_t> wake_detections_suppressed_{0};
  std::atomic<std::uint64_t> speech_frames_forwarded_{0};
  std::atomic<std::uint64_t> input_frames_paused_{0};
  std::atomic<std::uint64_t> kws_errors_{0};
  std::atomic<std::uint64_t> last_wake_timestamp_ms_{0};
};

const char* ToString(VoiceInputMode mode);

}  // namespace agent
}  // namespace cockpit
