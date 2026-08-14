#include "agent/runtime/voice_input_gate.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#include "agent/speech/asr/mock_speech_recognizer.h"
#include "agent/speech/kws/mock_wake_word_detector.h"
#include "agent/speech/kws/wake_prompt_player.h"
#include "agent/speech/pipeline/speech_pipeline.h"
#include "agent/speech/vad/mock_voice_activity_detector.h"
#include "cockpit/modules/audio/frames/audio_frame.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

namespace {

class FakeWakePromptPlayer final : public cockpit::agent::WakePromptPlayer {
 public:
  bool Play(std::string* error) override {
    if (error != nullptr) {
      error->clear();
    }
    played_ = true;
    return true;
  }

  bool played() const {
    return played_;
  }

 private:
  bool played_ = false;
};

cockpit::audio::AudioFrame MakeFrame(std::uint64_t sequence) {
  cockpit::audio::AudioFrame::Samples samples{};
  samples.fill(1000);
  return cockpit::audio::AudioFrame(sequence, static_cast<std::int64_t>(sequence * 20000000ULL),
                                    cockpit::audio::AudioFrameFlag::kNone, samples);
}

bool WaitForState(cockpit::voice::VoiceInteractionService& service,
                  cockpit::voice::InteractionState expected) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (service.state() == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return service.state() == expected;
}

}  // namespace

int main() {
  cockpit::config::AudioConfig audio_config;
  cockpit::config::SpeechSegmentConfig segment_config;
  segment_config.pre_roll_ms = 0;
  segment_config.max_segment_ms = 100;

  cockpit::agent::MockWakeWordDetector disabled_detector("你好小车");
  auto disabled_prompt = std::make_unique<FakeWakePromptPlayer>();
  cockpit::voice::VoiceInteractionService service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), nullptr);
  if (!service.Start()) {
    std::cerr << "failed to start voice interaction service\n";
    return 1;
  }

  cockpit::agent::SpeechPipeline speech_pipeline(
      audio_config, segment_config, std::make_unique<cockpit::agent::MockVoiceActivityDetector>(),
      std::make_unique<cockpit::voice::MockSpeechRecognizer>());
  std::string error;
  if (!speech_pipeline.Start(
          [](const cockpit::voice::SpeechTranscript&) {
          },
          &error)) {
    std::cerr << "failed to start speech pipeline: " << error << '\n';
    return 1;
  }

  cockpit::config::KwsConfig kws_disabled;
  kws_disabled.enabled = false;
  cockpit::agent::VoiceInputGate disabled_gate(
      kws_disabled, &service, &speech_pipeline,
      std::make_unique<cockpit::agent::MockWakeWordDetector>(), std::move(disabled_prompt));
  if (!disabled_gate.ProcessFrame(MakeFrame(1)) ||
      speech_pipeline.metrics().frames_processed != 1U) {
    std::cerr << "disabled KWS did not forward PCM to speech pipeline\n";
    return 1;
  }

  speech_pipeline.ResetInputState();
  service.Interrupt();
  if (!WaitForState(service, cockpit::voice::InteractionState::kIdle)) {
    std::cerr << "voice service did not return to idle after interrupt\n";
    return 1;
  }

  cockpit::config::KwsConfig kws_enabled;
  kws_enabled.enabled = true;
  kws_enabled.provider = "mock";
  kws_enabled.cooldown_ms = 1500;
  kws_enabled.wake_word = "你好小车";
  auto detector = std::make_unique<cockpit::agent::MockWakeWordDetector>("你好小车");
  auto* detector_ptr = detector.get();
  auto prompt = std::make_unique<FakeWakePromptPlayer>();
  auto* prompt_ptr = prompt.get();
  cockpit::agent::VoiceInputGate gate(kws_enabled, &service, &speech_pipeline, std::move(detector),
                                      std::move(prompt));
  detector_ptr->ArmAfterFrames(1U);
  const auto before_frames = speech_pipeline.metrics().frames_processed;
  if (!gate.ProcessFrame(MakeFrame(2))) {
    std::cerr << "enabled KWS rejected wake frame\n";
    return 1;
  }
  if (!WaitForState(service, cockpit::voice::InteractionState::kListening)) {
    std::cerr << "wake prompt did not advance the service to listening\n";
    return 1;
  }
  if (speech_pipeline.metrics().frames_processed != before_frames) {
    std::cerr << "wake frame leaked into speech pipeline\n";
    return 1;
  }
  if (!prompt_ptr->played()) {
    std::cerr << "wake prompt did not play\n";
    return 1;
  }

  if (!gate.ProcessFrame(MakeFrame(3)) ||
      speech_pipeline.metrics().frames_processed != before_frames + 1U) {
    std::cerr << "listening PCM did not forward to speech pipeline\n";
    return 1;
  }

  const auto wake_detections_before = gate.metrics().wake_detections;
  service.Interrupt();
  if (!WaitForState(service, cockpit::voice::InteractionState::kIdle)) {
    std::cerr << "voice service did not return to idle before cooldown check\n";
    return 1;
  }
  detector_ptr->ArmAfterFrames(1U);
  if (!gate.ProcessFrame(MakeFrame(4)) ||
      gate.metrics().wake_detections != wake_detections_before ||
      gate.metrics().wake_detections_suppressed == 0U) {
    std::cerr << "cooldown did not suppress repeated wake detection\n";
    return 1;
  }

  speech_pipeline.Stop();
  service.Stop();
  std::cout << "voice input gate test passed\n";
  return 0;
}
