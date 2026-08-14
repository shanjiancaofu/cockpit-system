#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "agent/speech/asr/speech_recognizer.h"
#include "agent/speech/segment/speech_segmenter.h"
#include "agent/speech/vad/voice_activity_detector.h"
#include "cockpit/core/base/macros.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/modules/audio/frames/spsc_ring_buffer.h"
#include "cockpit/modules/voice/assistant/speech_transcript.h"

namespace cockpit {
namespace agent {

struct SpeechPipelineMetrics {
  std::uint64_t frames_processed = 0;
  std::uint64_t speech_frames = 0;
  std::uint64_t segments_completed = 0;
  std::uint64_t segments_dropped = 0;
  std::uint64_t transcripts_published = 0;
  std::uint64_t asr_timeouts = 0;
  std::uint64_t errors = 0;
};

class SpeechPipeline {
 public:
  using TranscriptHandler = std::function<void(const voice::SpeechTranscript&)>;

  SpeechPipeline(config::AudioConfig audio_config, config::SpeechSegmentConfig segment_config,
                 std::unique_ptr<audio::VoiceActivityDetector> detector,
                 std::unique_ptr<voice::SpeechRecognizer> recognizer,
                 std::chrono::milliseconds recognition_timeout = std::chrono::seconds(3));
  ~SpeechPipeline();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(SpeechPipeline);

  bool Start(TranscriptHandler handler, std::string* error = nullptr);
  void Stop();
  bool Submit(const audio::AudioFrame& frame);
  SpeechPipelineMetrics metrics() const;
  std::string last_error() const;

 private:
  void PublishSegment(audio::SpeechSegment segment);
  void CancelActiveRecognition();
  void RecognizeSegments();
  void RecordError(std::string error);

  const config::AudioConfig audio_config_;
  const std::chrono::milliseconds recognition_timeout_;
  std::unique_ptr<audio::VoiceActivityDetector> detector_;
  std::unique_ptr<audio::SpeechSegmenter> segmenter_;
  std::unique_ptr<voice::SpeechRecognizer> recognizer_;
  audio::SpscRingBuffer<audio::SpeechSegment, 8> segments_;
  TranscriptHandler handler_;
  std::atomic_bool running_{false};
  std::atomic<std::uint64_t> lifecycle_generation_{0};
  std::atomic_bool recognition_active_{false};
  std::atomic_bool recognition_cancelled_{false};
  std::thread worker_;
  std::atomic<std::uint64_t> frames_processed_{0};
  std::atomic<std::uint64_t> speech_frames_{0};
  std::atomic<std::uint64_t> segments_completed_{0};
  std::atomic<std::uint64_t> transcripts_published_{0};
  std::atomic<std::uint64_t> asr_timeouts_{0};
  std::atomic<std::uint64_t> errors_{0};
  std::atomic<std::uint64_t> next_transcript_id_{1};
  mutable std::mutex error_mutex_;
  std::string last_error_;
};

}  // namespace agent
}  // namespace cockpit
