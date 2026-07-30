#include "agent/speech/pipeline/speech_pipeline.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit {
namespace agent {

SpeechPipeline::SpeechPipeline(config::AudioConfig audio_config,
                               config::SpeechSegmentConfig segment_config,
                               std::unique_ptr<audio::VoiceActivityDetector> detector,
                               std::unique_ptr<voice::SpeechRecognizer> recognizer)
    : audio_config_(std::move(audio_config)),
      detector_(std::move(detector)),
      recognizer_(std::move(recognizer)) {
  if (detector_ == nullptr || recognizer_ == nullptr) {
    throw std::invalid_argument("speech pipeline requires VAD and ASR implementations");
  }
  audio::SpeechSegmenterConfig config;
  config.pre_roll_frames =
      static_cast<std::size_t>(segment_config.pre_roll_ms / audio_config_.frame_ms);
  config.max_segment_frames =
      static_cast<std::size_t>(segment_config.max_segment_ms / audio_config_.frame_ms);
  segmenter_ = std::make_unique<audio::SpeechSegmenter>(config);
}

SpeechPipeline::~SpeechPipeline() {
  Stop();
}

bool SpeechPipeline::Start(TranscriptHandler handler, std::string* error) {
  if (running_.exchange(true)) {
    if (error != nullptr) {
      *error = "speech pipeline is already running";
    }
    return false;
  }
  handler_ = std::move(handler);
  if (!handler_) {
    running_.store(false);
    if (error != nullptr) {
      *error = "speech pipeline requires a transcript handler";
    }
    return false;
  }
  detector_->Reset();
  segmenter_->Reset();
  segments_.Reset();
  try {
    worker_ = std::thread(&SpeechPipeline::RecognizeSegments, this);
  } catch (const std::exception& exception) {
    running_.store(false);
    if (error != nullptr) {
      *error = exception.what();
    }
    return false;
  }
  return true;
}

void SpeechPipeline::Stop() {
  if (!running_.load()) {
    return;
  }
  auto final_segment = segmenter_->Flush();
  if (final_segment.has_value()) {
    PublishSegment(std::move(*final_segment));
  }
  running_.store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
  handler_ = {};
}

bool SpeechPipeline::Submit(const audio::AudioFrame& frame) {
  if (!running_.load()) {
    return false;
  }
  try {
    const audio::VoiceActivityResult activity = detector_->Analyze(frame);
    frames_processed_.fetch_add(1U);
    if (activity.state == audio::VoiceActivityState::kSpeech) {
      speech_frames_.fetch_add(1U);
    }
    auto segment = segmenter_->Process(frame, activity);
    if (segment.has_value()) {
      PublishSegment(std::move(*segment));
    }
    return true;
  } catch (const std::exception& exception) {
    RecordError(exception.what());
    detector_->Reset();
    segmenter_->Reset();
    return false;
  }
}

SpeechPipelineMetrics SpeechPipeline::metrics() const {
  return {frames_processed_.load(), speech_frames_.load(),         segments_completed_.load(),
          segments_.DropCount(),    transcripts_published_.load(), errors_.load()};
}

std::string SpeechPipeline::last_error() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

void SpeechPipeline::PublishSegment(audio::SpeechSegment segment) {
  segments_completed_.fetch_add(1U);
  segments_.TryPush(std::move(segment));
}

void SpeechPipeline::RecognizeSegments() {
  while (running_.load() || segments_.Available() != 0U) {
    auto segment = segments_.TryPop();
    if (!segment.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    try {
      const voice::SpeechRecognitionResult recognition = recognizer_->Recognize(*segment);
      if (!recognition.success) {
        RecordError(recognition.error.empty() ? "speech recognition failed" : recognition.error);
        continue;
      }
      voice::SpeechTranscript transcript;
      transcript.id = next_transcript_id_.fetch_add(1U);
      transcript.timestamp_ms = time::NowMs();
      transcript.start_sequence = segment->start_sequence;
      transcript.end_sequence = segment->end_sequence;
      transcript.duration_ms = segment->DurationMs();
      transcript.truncated = segment->truncated;
      transcript.discontinuous = segment->discontinuous;
      transcript.text = recognition.text;
      transcript.provider = recognition.provider;
      transcript.confidence = recognition.confidence;
      handler_(transcript);
      transcripts_published_.fetch_add(1U);
    } catch (const std::exception& exception) {
      RecordError(exception.what());
    }
  }
}

void SpeechPipeline::RecordError(std::string error) {
  errors_.fetch_add(1U);
  std::lock_guard<std::mutex> lock(error_mutex_);
  last_error_ = std::move(error);
}

}  // namespace agent
}  // namespace cockpit
