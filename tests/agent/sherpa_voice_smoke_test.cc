#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "agent/speech/kws/wake_word_detector.h"
#include "agent/speech/pipeline/speech_pipeline.h"
#include "agent/speech/providers/sherpa/sherpa_sensevoice_recognizer.h"
#include "agent/speech/providers/sherpa/sherpa_voice_activity_detector.h"
#include "agent/speech/providers/sherpa/sherpa_wake_word_detector.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/modules/audio/frames/audio_frame.h"
#include "cockpit/modules/audio/wav/wav_file.h"
#include "cockpit/modules/voice/assistant/deterministic_command_router.h"
#include "cockpit/modules/voice/assistant/transcript_normalizer.h"

namespace {

std::filesystem::path AiRoot() {
  const char* root = std::getenv("COCKPIT_AI_ROOT");
  if (root != nullptr && root[0] != '\0') {
    return std::filesystem::path(root);
  }
  return std::filesystem::path("_output") / "ai";
}

bool Check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

bool ReadFixture(const std::filesystem::path& path, cockpit::audio::PcmBuffer* buffer) {
  std::string error;
  if (!cockpit::audio::ReadPcm16Wav(path.string(), buffer, &error)) {
    std::cerr << "failed to read WAV fixture " << path << ": " << error << '\n';
    return false;
  }
  if (buffer->format.sample_rate_hz !=
          static_cast<int>(cockpit::audio::AudioFrame::kSampleRateHz) ||
      buffer->format.channels != static_cast<int>(cockpit::audio::AudioFrame::kChannels)) {
    std::cerr << "WAV fixture must be 16 kHz mono PCM16: " << path << '\n';
    return false;
  }
  return true;
}

std::vector<cockpit::audio::AudioFrame> ToFrames(const cockpit::audio::PcmBuffer& buffer) {
  std::vector<cockpit::audio::AudioFrame> frames;
  const std::size_t samples_per_frame = cockpit::audio::AudioFrame::kSampleCount;
  const std::size_t frame_count =
      (buffer.samples.size() + samples_per_frame - 1U) / samples_per_frame;
  frames.reserve(frame_count);
  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    cockpit::audio::AudioFrame::Samples samples{};
    const std::size_t offset = frame_index * samples_per_frame;
    const std::size_t remaining = buffer.samples.size() - offset;
    const std::size_t copy_count = std::min(samples_per_frame, remaining);
    for (std::size_t sample_index = 0; sample_index < copy_count; ++sample_index) {
      samples[sample_index] = buffer.samples[offset + sample_index];
    }
    frames.emplace_back(static_cast<std::uint64_t>(frame_index),
                        static_cast<std::int64_t>(frame_index * 20'000'000ULL),
                        cockpit::audio::AudioFrameFlag::kNone, samples);
  }
  return frames;
}

bool DetectWakeWord(const std::filesystem::path& wav_path,
                    cockpit::agent::WakeWordDetector* detector,
                    cockpit::agent::WakeWordResult* detection) {
  cockpit::audio::PcmBuffer buffer;
  if (!ReadFixture(wav_path, &buffer)) {
    return false;
  }
  detector->Reset();
  for (const auto& frame : ToFrames(buffer)) {
    const auto result = detector->Analyze(frame);
    if (!result.error.empty()) {
      std::cerr << "KWS failed for " << wav_path << ": " << result.error << '\n';
      return false;
    }
    if (result.detected) {
      *detection = result;
      return true;
    }
  }
  *detection = {};
  return true;
}

bool RunOpenCameraPipeline(const std::filesystem::path& wav_path,
                           const std::filesystem::path& trailing_silence_path) {
  cockpit::audio::PcmBuffer buffer;
  if (!ReadFixture(wav_path, &buffer)) {
    return false;
  }
  cockpit::audio::PcmBuffer trailing_silence;
  if (!ReadFixture(trailing_silence_path, &trailing_silence)) {
    return false;
  }

  cockpit::config::AudioConfig audio_config;
  cockpit::config::SpeechSegmentConfig segment_config;
  segment_config.pre_roll_ms = 0;
  segment_config.max_segment_ms = 15000;

  cockpit::agent::SpeechPipeline pipeline(
      audio_config, segment_config, cockpit::agent::CreateSherpaVoiceActivityDetector(),
      cockpit::voice::CreateSherpaSenseVoiceRecognizer(), std::chrono::seconds(10));

  std::mutex mutex;
  std::condition_variable changed;
  std::optional<cockpit::voice::SpeechTranscript> transcript;
  std::string error;
  if (!pipeline.Start(
          [&](const cockpit::voice::SpeechTranscript& value) {
            {
              std::lock_guard<std::mutex> lock(mutex);
              transcript = value;
            }
            changed.notify_all();
          },
          &error)) {
    std::cerr << "failed to start Sherpa speech pipeline: " << error << '\n';
    return false;
  }

  for (const auto& frame : ToFrames(buffer)) {
    if (!pipeline.Submit(frame)) {
      std::cerr << "failed to submit frame to Sherpa speech pipeline: " << pipeline.last_error()
                << '\n';
      pipeline.Stop();
      return false;
    }
  }
  const std::uint64_t command_frame_count = static_cast<std::uint64_t>(ToFrames(buffer).size());
  for (auto frame : ToFrames(trailing_silence)) {
    cockpit::audio::AudioFrame::Samples samples{};
    samples = frame.samples();
    const auto sequence = command_frame_count + frame.sequence();
    const cockpit::audio::AudioFrame shifted(sequence,
                                             static_cast<std::int64_t>(sequence * 20'000'000ULL),
                                             cockpit::audio::AudioFrameFlag::kNone, samples);
    if (!pipeline.Submit(shifted)) {
      std::cerr << "failed to submit trailing silence to Sherpa speech pipeline: "
                << pipeline.last_error() << '\n';
      pipeline.Stop();
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (transcript.has_value()) {
        break;
      }
    }
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::seconds(10), [&transcript] {
      return transcript.has_value();
    });
  }
  pipeline.Stop();

  const auto metrics = pipeline.metrics();
  if (!Check(metrics.frames_processed != 0U, "Sherpa pipeline did not process frames") ||
      !Check(metrics.speech_frames != 0U, "Sherpa VAD did not detect speech") ||
      !Check(metrics.segments_completed != 0U, "SpeechSegmenter did not complete a segment") ||
      !Check(metrics.transcripts_published != 0U, "Sherpa ASR did not publish a transcript") ||
      !Check(transcript.has_value(), "missing Sherpa ASR transcript")) {
    std::cerr << "Sherpa pipeline metrics: frames=" << metrics.frames_processed
              << " speech=" << metrics.speech_frames << " segments=" << metrics.segments_completed
              << " transcripts=" << metrics.transcripts_published << " errors=" << metrics.errors
              << " last_error=" << pipeline.last_error() << '\n';
    return false;
  }

  const std::string normalized = cockpit::voice::TranscriptNormalizer::Normalize(transcript->text);
  const auto route = cockpit::voice::DeterministicCommandRouter().Route(normalized);
  std::cout << "SenseVoice transcript: " << transcript->text << '\n';
  std::cout << "normalized transcript: " << normalized << '\n';
  if (route.intent != cockpit::voice::VoiceIntent::kOpenCamera ||
      route.action != cockpit::voice::VoiceAction::kOpenCamera) {
    std::cerr << "transcript did not route to OpenCamera: intent="
              << cockpit::voice::ToString(route.intent)
              << " action=" << cockpit::voice::ToString(route.action) << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::filesystem::path ai_root = AiRoot();
  const std::filesystem::path fixture_root = ai_root / "fixtures";
  const std::filesystem::path kws_model =
      ai_root / "models" / "kws" / "sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20";
  const std::filesystem::path keywords_file = ai_root / "config" / "kws-keywords.txt";

  cockpit::config::KwsConfig kws_config;
  kws_config.enabled = true;
  kws_config.provider = "sherpa";
  kws_config.model_dir = kws_model.string();
  kws_config.keywords_file = keywords_file.string();
  kws_config.wake_word.clear();

  auto wake_detector = cockpit::agent::CreateSherpaWakeWordDetector(kws_config);

  cockpit::agent::WakeWordResult wake_result;
  if (!DetectWakeWord(fixture_root / "nihao-xiaoche.wav", wake_detector.get(), &wake_result)) {
    return 1;
  }
  if (!Check(wake_result.detected, "Sherpa KWS did not detect 你好小车")) {
    return 1;
  }
  std::cout << "KWS detected: " << wake_result.keyword << '\n';

  cockpit::agent::WakeWordResult silence_result;
  if (!DetectWakeWord(fixture_root / "silence.wav", wake_detector.get(), &silence_result)) {
    return 1;
  }
  if (!Check(!silence_result.detected, "Sherpa KWS falsely detected wake word on silence")) {
    return 1;
  }

  cockpit::agent::WakeWordResult command_result;
  if (!DetectWakeWord(fixture_root / "open-camera-zh.wav", wake_detector.get(), &command_result)) {
    return 1;
  }
  if (!Check(!command_result.detected,
             "Sherpa KWS falsely detected wake word on command-only speech")) {
    return 1;
  }

  if (!RunOpenCameraPipeline(fixture_root / "open-camera-zh.wav", fixture_root / "silence.wav")) {
    return 1;
  }

  std::cout << "Sherpa voice x86_64 smoke passed\n";
  return 0;
}
