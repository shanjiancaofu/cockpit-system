#include "agent/runtime/voice_input_gate.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
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
    {
      std::unique_lock<std::mutex> lock(mutex_);
      started_ = true;
      changed_.notify_all();
      changed_.wait(lock, [this] {
        return !block_ || stopped_;
      });
    }
    if (stopped_.load()) {
      if (error != nullptr) {
        *error = "fake wake prompt stopped";
      }
      return false;
    }
    if (error != nullptr) {
      error->clear();
    }
    played_.store(true);
    return succeeds_;
  }

  void Stop() override {
    stopped_.store(true);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      block_ = false;
    }
    changed_.notify_all();
  }

  void BlockUntilReleased() {
    std::lock_guard<std::mutex> lock(mutex_);
    block_ = true;
  }

  bool WaitUntilStarted() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(1), [this] {
      return started_;
    });
  }

  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      block_ = false;
    }
    changed_.notify_all();
  }

  void set_succeeds(bool succeeds) {
    succeeds_ = succeeds;
  }

  bool played() const {
    return played_.load();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool block_ = false;
  bool started_ = false;
  bool succeeds_ = true;
  std::atomic_bool played_{false};
  std::atomic_bool stopped_{false};
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

  cockpit::agent::MockWakeWordDetector disabled_detector("你好小山");
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
  kws_enabled.wake_word = "你好小山";
  auto detector = std::make_unique<cockpit::agent::MockWakeWordDetector>("你好小山");
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

  service.Interrupt();
  if (!WaitForState(service, cockpit::voice::InteractionState::kIdle)) {
    std::cerr << "voice service did not return to idle before async wake prompt check\n";
    return 1;
  }
  cockpit::config::KwsConfig async_kws = kws_enabled;
  async_kws.cooldown_ms = 1;
  auto async_detector = std::make_unique<cockpit::agent::MockWakeWordDetector>("你好小山");
  auto* async_detector_ptr = async_detector.get();
  auto async_prompt = std::make_unique<FakeWakePromptPlayer>();
  auto* async_prompt_ptr = async_prompt.get();
  async_prompt_ptr->BlockUntilReleased();
  cockpit::agent::VoiceInputGate async_gate(async_kws, &service, &speech_pipeline,
                                            std::move(async_detector), std::move(async_prompt));
  async_detector_ptr->ArmAfterFrames(1U);
  const auto async_before_frames = speech_pipeline.metrics().frames_processed;
  if (!async_gate.ProcessFrame(MakeFrame(5)) || !async_prompt_ptr->WaitUntilStarted()) {
    std::cerr << "async wake prompt did not start\n";
    return 1;
  }
  if (service.state() != cockpit::voice::InteractionState::kWaking) {
    std::cerr << "service did not remain waking while wake prompt was active\n";
    return 1;
  }
  if (!async_gate.ProcessFrame(MakeFrame(6)) ||
      speech_pipeline.metrics().frames_processed != async_before_frames) {
    std::cerr << "PCM was not drained/paused during async wake prompt\n";
    return 1;
  }
  async_prompt_ptr->Release();
  if (!WaitForState(service, cockpit::voice::InteractionState::kListening)) {
    std::cerr << "async wake prompt did not advance to listening\n";
    return 1;
  }
  async_gate.Stop();

  service.Interrupt();
  if (!WaitForState(service, cockpit::voice::InteractionState::kIdle)) {
    std::cerr << "voice service did not return to idle before wake failure check\n";
    return 1;
  }
  auto fail_detector = std::make_unique<cockpit::agent::MockWakeWordDetector>("你好小山");
  auto* fail_detector_ptr = fail_detector.get();
  auto fail_prompt = std::make_unique<FakeWakePromptPlayer>();
  auto* fail_prompt_ptr = fail_prompt.get();
  fail_prompt_ptr->set_succeeds(false);
  cockpit::agent::VoiceInputGate fail_gate(async_kws, &service, &speech_pipeline,
                                           std::move(fail_detector), std::move(fail_prompt));
  fail_detector_ptr->ArmAfterFrames(1U);
  if (!fail_gate.ProcessFrame(MakeFrame(7))) {
    std::cerr << "wake failure frame was rejected\n";
    return 1;
  }
  if (!WaitForState(service, cockpit::voice::InteractionState::kIdle)) {
    std::cerr << "wake prompt failure entered listening or did not recover\n";
    return 1;
  }
  fail_gate.Stop();

  speech_pipeline.Stop();
  service.Stop();
  std::cout << "voice input gate test passed\n";
  return 0;
}
