#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "agent/runtime/voice_input_gate.h"
#include "agent/speech/providers/sherpa/sherpa_sensevoice_recognizer.h"
#include "agent/speech/providers/sherpa/sherpa_voice_activity_detector.h"
#include "agent/speech/providers/sherpa/sherpa_wake_word_detector.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/modules/audio/frames/audio_frame.h"
#include "cockpit/modules/audio/wav/wav_file.h"
#include "cockpit/modules/voice/actions/mock_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

namespace {

std::filesystem::path AiRoot() {
  const char* root = std::getenv("COCKPIT_AI_ROOT");
  return root != nullptr && root[0] != '\0' ? std::filesystem::path(root)
                                            : std::filesystem::path("_output") / "ai";
}

bool ReadFrames(const std::filesystem::path& path,
                std::vector<cockpit::audio::AudioFrame>* frames) {
  cockpit::audio::PcmBuffer buffer;
  std::string error;
  if (!cockpit::audio::ReadPcm16Wav(path.string(), &buffer, &error)) {
    std::cerr << "failed to read " << path << ": " << error << '\n';
    return false;
  }
  if (buffer.format.sample_rate_hz != 16000 || buffer.format.channels != 1) {
    std::cerr << "fixture must be 16 kHz mono PCM16: " << path << '\n';
    return false;
  }
  constexpr std::size_t kSamples = cockpit::audio::AudioFrame::kSampleCount;
  const std::size_t count = (buffer.samples.size() + kSamples - 1U) / kSamples;
  frames->clear();
  frames->reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    cockpit::audio::AudioFrame::Samples samples{};
    const std::size_t offset = i * kSamples;
    const std::size_t copy = std::min(kSamples, buffer.samples.size() - offset);
    std::copy_n(buffer.samples.data() + offset, copy, samples.data());
    frames->emplace_back(static_cast<std::uint64_t>(i),
                         static_cast<std::int64_t>(i * 20'000'000ULL),
                         cockpit::audio::AudioFrameFlag::kNone, samples);
  }
  return true;
}

bool WaitForListening(cockpit::voice::VoiceInteractionService& service) {
  for (int i = 0; i < 200; ++i) {
    if (service.state() == cockpit::voice::InteractionState::kListening) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return service.state() == cockpit::voice::InteractionState::kListening;
}

bool RunGateCase(const std::filesystem::path& wav, bool expect_wake, bool expect_action) {
  std::vector<cockpit::audio::AudioFrame> frames;
  if (!ReadFrames(wav, &frames)) return false;
  std::vector<cockpit::audio::AudioFrame> silence;
  if (!ReadFrames(AiRoot() / "fixtures" / "silence.wav", &silence)) return false;

  const auto ai_root = AiRoot();
  cockpit::config::KwsConfig kws;
  kws.enabled = true;
  kws.provider = "sherpa";
  kws.cooldown_ms = 0;
  kws.wake_word.clear();
  kws.model_dir = (ai_root / "models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20").string();
  kws.keywords_file = (ai_root / "config/kws-keywords.txt").string();

  cockpit::config::AudioConfig audio;
  cockpit::config::SpeechSegmentConfig segment;
  segment.pre_roll_ms = 300;
  segment.max_segment_ms = 15000;
  cockpit::voice::VoiceInteractionService service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::make_unique<cockpit::voice::MockActionDispatcher>());
  if (!service.Start()) {
    std::cerr << "failed to start VoiceInteractionService\n";
    return false;
  }
  cockpit::agent::SpeechPipeline pipeline(
      audio, segment, cockpit::agent::CreateSherpaVoiceActivityDetector(),
      cockpit::voice::CreateSherpaSenseVoiceRecognizer(), std::chrono::seconds(10));
  std::string error;
  if (!pipeline.Start([&](const cockpit::voice::SpeechTranscript& transcript) {
        service.SubmitTranscript(transcript);
      }, &error)) {
    std::cerr << "failed to start speech pipeline: " << error << '\n';
    service.Stop();
    return false;
  }
  cockpit::agent::VoiceInputGate gate(
      kws, &service, &pipeline, cockpit::agent::CreateSherpaWakeWordDetector(kws), nullptr);

  for (const auto& frame : frames) {
    if (!gate.ProcessFrame(frame)) {
      std::cerr << "gate rejected frame: " << pipeline.last_error() << '\n';
      pipeline.Stop();
      service.Stop();
      return false;
    }
    if (service.state() == cockpit::voice::InteractionState::kWaking &&
        !WaitForListening(service)) {
      std::cerr << "wake prompt did not complete\n";
      pipeline.Stop();
      service.Stop();
      return false;
    }
  }
  // Keep the pipeline running while VAD emits the final segment and ASR publishes it.
  for (const auto& frame : silence) {
    if (!gate.ProcessFrame(frame)) break;
  }
  for (int i = 0; i < 1000; ++i) {
    const auto metrics = pipeline.metrics();
    if (metrics.segments_completed != 0U &&
        metrics.transcripts_published >= metrics.segments_completed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  pipeline.Stop();
  const auto gate_metrics = gate.metrics();
  const auto status = service.status();
  service.Stop();

  std::cout << wav.filename() << ": mode=" << cockpit::agent::ToString(gate.mode())
            << " kws_frames=" << gate_metrics.kws_frames_processed
            << " wakes=" << gate_metrics.wake_detections
            << " segments=" << pipeline.metrics().segments_completed
            << " transcripts=" << pipeline.metrics().transcripts_published
            << " actions=" << status.metrics.actions_succeeded << '\n';
  if (expect_wake != (gate_metrics.wake_detections != 0U)) return false;
  if (expect_action != (status.metrics.actions_succeeded != 0U)) return false;
  return true;
}

}  // namespace

int main() {
  const auto fixtures = AiRoot() / "fixtures";
  if (!RunGateCase(fixtures / "live" / "segment-02-wake-open-camera.wav", true, true)) return 1;
  if (!RunGateCase(fixtures / "nihao-xiaoche.wav", false, false)) return 1;
  std::cout << "Sherpa voice Gate x86_64 smoke passed\n";
  return 0;
}
