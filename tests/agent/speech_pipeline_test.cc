#include "agent/speech/pipeline/speech_pipeline.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>

#include "agent/speech/asr/mock_speech_recognizer.h"
#include "agent/speech/vad/mock_voice_activity_detector.h"

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
    changed.wait_for(lock, std::chrono::seconds(1), [&transcript] {
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
  std::cout << "agent speech pipeline tests passed\n";
  return 0;
}
