#include "agent/runtime/voice_input_gate.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit {
namespace agent {

VoiceInputGate::VoiceInputGate(config::KwsConfig config, voice::VoiceInteractionService* service,
                               SpeechPipeline* speech_pipeline,
                               std::unique_ptr<WakeWordDetector> detector,
                               std::unique_ptr<WakePromptPlayer> prompt_player)
    : config_(std::move(config)),
      service_(service),
      speech_pipeline_(speech_pipeline),
      detector_(std::move(detector)),
      prompt_player_(std::move(prompt_player)) {
  if (service_ == nullptr || speech_pipeline_ == nullptr) {
    throw std::invalid_argument("voice input gate requires service and speech pipeline");
  }
  if (config_.enabled && detector_ == nullptr) {
    throw std::invalid_argument("voice input gate requires a wake detector when KWS is enabled");
  }
  if (prompt_player_ == nullptr) {
    prompt_player_ = std::make_unique<NoopWakePromptPlayer>();
  }
}

VoiceInputGate::~VoiceInputGate() {
  Stop();
}

bool VoiceInputGate::ProcessFrame(const audio::AudioFrame& frame) {
  if (stopping_.load()) {
    return false;
  }
  const VoiceInputMode next_mode = DetermineMode();
  ResetSpeechInputIfLeavingSpeech(next_mode);
  last_mode_ = next_mode;
  switch (next_mode) {
    case VoiceInputMode::kSpeech:
      speech_frames_forwarded_.fetch_add(1U);
      return speech_pipeline_->Submit(frame);
    case VoiceInputMode::kPaused:
      input_frames_paused_.fetch_add(1U);
      return true;
    case VoiceInputMode::kKws:
      break;
  }

  try {
    WakeWordResult result = detector_->Analyze(frame);
    kws_frames_processed_.fetch_add(1U);
    if (!result.error.empty()) {
      kws_errors_.fetch_add(1U);
      service_->SetLastError(result.error);
      return false;
    }
    if (!result.detected) {
      return true;
    }
    if (!AcceptWakeDetection()) {
      wake_detections_suppressed_.fetch_add(1U);
      return true;
    }
    wake_detections_.fetch_add(1U);
    last_wake_timestamp_ms_.store(
        static_cast<std::uint64_t>(time::WallTime::Now().ToMilliseconds()));
    detector_->Reset();
    speech_pipeline_->ResetInputState();
    if (!service_->NotifyWakeWordDetected()) {
      wake_detections_suppressed_.fetch_add(1U);
      return true;
    }
    if (!StartWakePrompt()) {
      service_->NotifyWakePromptFailed("wake prompt playback is already active or stopping");
    }
    return true;
  } catch (const std::exception& exception) {
    kws_errors_.fetch_add(1U);
    service_->SetLastError(exception.what());
    detector_->Reset();
    return false;
  }
}

void VoiceInputGate::Stop() {
  stopping_.store(true);
  wake_prompt_generation_.fetch_add(1U);
  if (prompt_player_ != nullptr) {
    prompt_player_->Stop();
  }
  JoinWakePrompt();
}

VoiceInputGateMetrics VoiceInputGate::metrics() const {
  return {kws_frames_processed_.load(),       wake_detections_.load(),
          wake_detections_suppressed_.load(), speech_frames_forwarded_.load(),
          input_frames_paused_.load(),        kws_errors_.load(),
          last_wake_timestamp_ms_.load()};
}

VoiceInputMode VoiceInputGate::mode() const {
  return DetermineMode();
}

VoiceInputMode VoiceInputGate::DetermineMode() const {
  if (!config_.enabled) {
    return VoiceInputMode::kSpeech;
  }
  switch (service_->state()) {
    case voice::InteractionState::kIdle:
      return VoiceInputMode::kKws;
    case voice::InteractionState::kListening:
    case voice::InteractionState::kFollowUp:
      return VoiceInputMode::kSpeech;
    case voice::InteractionState::kDisabled:
    case voice::InteractionState::kWaking:
    case voice::InteractionState::kRecognizing:
    case voice::InteractionState::kRouting:
    case voice::InteractionState::kExecuting:
    case voice::InteractionState::kThinking:
    case voice::InteractionState::kSpeaking:
    case voice::InteractionState::kCancelled:
    case voice::InteractionState::kErrorRecovery:
    case voice::InteractionState::kShuttingDown:
      return VoiceInputMode::kPaused;
  }
  return VoiceInputMode::kPaused;
}

void VoiceInputGate::ResetSpeechInputIfLeavingSpeech(VoiceInputMode next_mode) {
  if (last_mode_ == VoiceInputMode::kSpeech && next_mode != VoiceInputMode::kSpeech) {
    speech_pipeline_->ResetInputState();
  }
}

bool VoiceInputGate::AcceptWakeDetection() {
  const auto now = std::chrono::steady_clock::now();
  if (has_last_wake_ && now - last_wake_ < std::chrono::milliseconds(config_.cooldown_ms)) {
    return false;
  }
  last_wake_ = now;
  has_last_wake_ = true;
  return true;
}

bool VoiceInputGate::StartWakePrompt() {
  std::lock_guard<std::mutex> lock(wake_prompt_mutex_);
  if (stopping_.load()) {
    return false;
  }
  if (wake_prompt_worker_.joinable()) {
    if (!wake_prompt_done_.load()) {
      return false;
    }
    wake_prompt_worker_.join();
  }
  wake_prompt_done_.store(false);
  const std::uint64_t generation = wake_prompt_generation_.fetch_add(1U) + 1U;
  wake_prompt_worker_ = std::thread([this, generation] {
    std::string prompt_error;
    const bool played = prompt_player_->Play(&prompt_error);
    wake_prompt_done_.store(true);
    if (stopping_.load() || generation != wake_prompt_generation_.load()) {
      return;
    }
    if (played) {
      service_->NotifyWakePromptCompleted();
      return;
    }
    service_->NotifyWakePromptFailed(prompt_error.empty() ? "wake prompt playback failed"
                                                          : std::move(prompt_error));
  });
  return true;
}

void VoiceInputGate::JoinWakePrompt() {
  std::lock_guard<std::mutex> lock(wake_prompt_mutex_);
  if (wake_prompt_worker_.joinable()) {
    wake_prompt_worker_.join();
  }
}

const char* ToString(VoiceInputMode mode) {
  switch (mode) {
    case VoiceInputMode::kKws:
      return "kws";
    case VoiceInputMode::kSpeech:
      return "speech";
    case VoiceInputMode::kPaused:
      return "paused";
  }
  return "unknown";
}

}  // namespace agent
}  // namespace cockpit
