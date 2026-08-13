#include "agent/speech/pipeline/speech_pipeline.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "agent/speech/asr/mock_speech_recognizer.h"
#include "agent/speech/vad/mock_voice_activity_detector.h"

namespace {

class CancellableRecognizer final : public cockpit::voice::SpeechRecognizer {
 public:
  cockpit::voice::SpeechRecognitionResult Recognize(
      const cockpit::audio::SpeechSegment&,
      std::chrono::steady_clock::time_point deadline) override {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    const auto remaining = deadline - std::chrono::steady_clock::now();
    changed_.wait_until(lock, std::chrono::system_clock::now() + remaining, [this] {
      return cancelled_;
    });
    return {false, {}, "cancellable", 0.0F, cancelled_ ? "cancelled" : "deadline exceeded"};
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
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
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
  cockpit::config::AudioConfig audio_config;
  cockpit::config::SpeechSegmentConfig segment_config;
  segment_config.pre_roll_ms = 0;
  segment_config.max_segment_ms = 100;
  cockpit::agent::SpeechPipeline pipeline(
      audio_config, segment_config, std::make_unique<cockpit::agent::MockVoiceActivityDetector>(),
      std::make_unique<cockpit::voice::MockSpeechRecognizer>());

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
    std::cerr << "failed to start speech pipeline: " << error << '\n';
    return 1;
  }
  for (std::uint64_t sequence = 0; sequence < 5; ++sequence) {
    cockpit::audio::AudioFrame::Samples samples{};
    samples.fill(10000);
    pipeline.Submit(cockpit::audio::AudioFrame(sequence,
                                               static_cast<std::int64_t>(sequence * 20000000ULL),
                                               cockpit::audio::AudioFrameFlag::kNone, samples));
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                       [&transcript] {
                         return transcript.has_value();
                       });
  }
  pipeline.Stop();
  const auto metrics = pipeline.metrics();
  if (!transcript.has_value() || transcript->text != "mock transcript duration_ms=100" ||
      transcript->provider != "mock" || transcript->duration_ms != 100 ||
      metrics.frames_processed != 5 || metrics.segments_completed != 1 ||
      metrics.transcripts_published != 1 || metrics.errors != 0) {
    std::cerr << "speech pipeline result is invalid\n";
    return 1;
  }

  segment_config.max_segment_ms = 20;
  auto cancellable_recognizer = std::make_unique<CancellableRecognizer>();
  auto* recognizer_observer = cancellable_recognizer.get();
  cockpit::agent::SpeechPipeline cancellable_pipeline(
      audio_config, segment_config, std::make_unique<cockpit::agent::MockVoiceActivityDetector>(),
      std::move(cancellable_recognizer), std::chrono::seconds(10));
  if (!cancellable_pipeline.Start(
          [](const cockpit::voice::SpeechTranscript&) {
          },
          &error)) {
    std::cerr << "failed to start cancellable speech pipeline: " << error << '\n';
    return 1;
  }
  cockpit::audio::AudioFrame::Samples samples{};
  samples.fill(10000);
  cancellable_pipeline.Submit(
      cockpit::audio::AudioFrame(0, 0, cockpit::audio::AudioFrameFlag::kNone, samples));
  if (!recognizer_observer->WaitUntilEntered()) {
    std::cerr << "speech recognizer did not start\n";
    return 1;
  }
  const auto stop_started = std::chrono::steady_clock::now();
  cancellable_pipeline.Stop();
  if (std::chrono::steady_clock::now() - stop_started > std::chrono::milliseconds(300) ||
      cancellable_pipeline.metrics().errors != 1) {
    std::cerr << "speech recognition cancellation was not bounded\n";
    return 1;
  }
  std::cout << "agent speech pipeline tests passed\n";
  return 0;
}
