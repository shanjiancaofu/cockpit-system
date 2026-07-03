#include "audio_service.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#include "drivers/alsa/alsa_capture_source.h"
#include "modules/audio/vad/energy_vad.h"

namespace cockpit {
namespace audio {
namespace {

PcmFormat ToPcmFormat(const config::AudioConfig& config) {
  PcmFormat format;
  format.sample_rate_hz = config.sample_rate_hz;
  format.channels = config.channels;
  format.frame_ms = config.frame_ms;
  return format;
}

AudioService::SourceFactory DefaultSourceFactory() {
  return [](const std::string& device, const PcmFormat& format) {
    return std::make_unique<AlsaCaptureSource>(device, format);
  };
}

}  // namespace

AudioService::AudioService(config::AudioConfig config)
    : AudioService(std::move(config), config::VadConfig{}, config::SpeechSegmentConfig{},
                   DefaultSourceFactory()) {
}

AudioService::AudioService(config::AudioConfig config, SourceFactory source_factory)
    : AudioService(std::move(config), config::VadConfig{}, config::SpeechSegmentConfig{},
                   std::move(source_factory)) {
}

AudioService::AudioService(config::AudioConfig config, config::VadConfig vad_config)
    : AudioService(std::move(config), std::move(vad_config), config::SpeechSegmentConfig{},
                   DefaultSourceFactory()) {
}

AudioService::AudioService(config::AudioConfig config, config::VadConfig vad_config,
                           SourceFactory source_factory)
    : AudioService(std::move(config), std::move(vad_config), config::SpeechSegmentConfig{},
                   std::move(source_factory)) {
}

AudioService::AudioService(config::AudioConfig config, config::VadConfig vad_config,
                           config::SpeechSegmentConfig segment_config)
    : AudioService(std::move(config), std::move(vad_config), segment_config,
                   DefaultSourceFactory()) {
}

AudioService::AudioService(config::AudioConfig config, config::VadConfig vad_config,
                           config::SpeechSegmentConfig segment_config,
                           std::unique_ptr<voice::SpeechRecognizer> recognizer)
    : AudioService(std::move(config), std::move(vad_config), segment_config, DefaultSourceFactory(),
                   std::move(recognizer)) {
}

AudioService::AudioService(config::AudioConfig config, config::VadConfig vad_config,
                           config::SpeechSegmentConfig segment_config, SourceFactory source_factory)
    : AudioService(std::move(config), std::move(vad_config), segment_config,
                   std::move(source_factory), nullptr) {
}

AudioService::AudioService(config::AudioConfig config, config::VadConfig vad_config,
                           config::SpeechSegmentConfig segment_config, SourceFactory source_factory,
                           std::unique_ptr<voice::SpeechRecognizer> recognizer)
    : config_(std::move(config)),
      vad_config_(std::move(vad_config)),
      segment_config_(segment_config),
      source_factory_(std::move(source_factory)),
      recognizer_(std::move(recognizer)) {
  auto capture_module = std::make_unique<AudioCaptureModule>();
  capture_module_ = capture_module.get();
  module_manager_.Add(std::move(capture_module));

  if (vad_config_.enabled) {
    EnergyVadConfig energy_config;
    energy_config.speech_threshold_dbfs = vad_config_.speech_threshold_dbfs;
    energy_config.speech_start_frames = static_cast<std::uint32_t>(vad_config_.speech_start_frames);
    energy_config.speech_end_frames = static_cast<std::uint32_t>(vad_config_.speech_end_frames);
    vad_ = std::make_unique<EnergyVad>(energy_config);
    SpeechSegmenterConfig segmenter_config;
    segmenter_config.pre_roll_frames =
        static_cast<std::size_t>(segment_config_.pre_roll_ms / config_.frame_ms);
    segmenter_config.max_segment_frames =
        static_cast<std::size_t>(segment_config_.max_segment_ms / config_.frame_ms);
    segmenter_ = std::make_unique<SpeechSegmenter>(segmenter_config);
  }
}

AudioService::~AudioService() {
  StopCapture();
}

bool AudioService::StartCapture(const std::string& input_device, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const AudioCaptureState state = CaptureStateLocked();
  if (state != AudioCaptureState::kStopped && state != AudioCaptureState::kFaulted) {
    if (error != nullptr) {
      *error = "audio capture is already active";
    }
    return false;
  }
  StopCaptureLocked();

  input_device_ = input_device.empty() ? config_.input_device : input_device;
  auto source = source_factory_(input_device_, ToPcmFormat(config_));
  if (source == nullptr) {
    if (error != nullptr) {
      *error = "audio capture source factory returned null";
    }
    return false;
  }
  capture_module_->Configure(std::make_unique<AudioCaptureStream>(std::move(source)));
  if (!module_manager_.StartAll()) {
    if (error != nullptr) {
      *error = capture_module_->last_error();
    }
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (capture_module_->capture_state() == AudioCaptureState::kStarting &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (capture_module_->capture_state() == AudioCaptureState::kFaulted) {
    if (error != nullptr) {
      *error = capture_module_->last_error();
    }
    return false;
  }
  ResetVadMetrics();
  ResetAsrMetrics();
  speech_segments_.Reset();
  if (recognizer_ != nullptr) {
    asr_stop_.store(false);
    try {
      asr_worker_ = std::thread(&AudioService::ProcessSpeechSegments, this);
    } catch (const std::exception& exception) {
      module_manager_.StopAll();
      if (error != nullptr) {
        *error = exception.what();
      }
      return false;
    }
  }
  if (vad_ != nullptr) {
    vad_->Reset();
    segmenter_->Reset();
    vad_stop_.store(false);
    try {
      vad_worker_ = std::thread(&AudioService::ProcessVoiceActivity, this);
    } catch (const std::exception& exception) {
      module_manager_.StopAll();
      asr_stop_.store(true);
      if (asr_worker_.joinable()) {
        asr_worker_.join();
      }
      if (error != nullptr) {
        *error = exception.what();
      }
      return false;
    }
  }
  return true;
}

void AudioService::StopCapture() {
  std::lock_guard<std::mutex> lock(mutex_);
  StopCaptureLocked();
}

std::optional<SpeechSegment> AudioService::TryPopSpeechSegment() {
  if (recognizer_ != nullptr) {
    return std::nullopt;
  }
  return speech_segments_.TryPop();
}

bool AudioService::WaitForTranscript(std::uint64_t after_id, std::chrono::milliseconds timeout,
                                     voice::SpeechTranscript* transcript) const {
  std::unique_lock<std::mutex> lock(transcript_mutex_);
  transcript_changed_.wait_for(lock, timeout, [this, after_id] {
    return !transcript_history_.empty() && transcript_history_.back().id > after_id;
  });
  const auto next = std::find_if(transcript_history_.begin(), transcript_history_.end(),
                                 [after_id](const voice::SpeechTranscript& value) {
                                   return value.id > after_id;
                                 });
  if (next == transcript_history_.end()) {
    return false;
  }
  if (transcript != nullptr) {
    *transcript = *next;
  }
  return true;
}

AudioServiceStatus AudioService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioServiceStatus result;
  result.input_device = input_device_.empty() ? config_.input_device : input_device_;
  result.sample_rate_hz = static_cast<std::uint32_t>(config_.sample_rate_hz);
  result.channels = static_cast<std::uint32_t>(config_.channels);
  result.frame_ms = static_cast<std::uint32_t>(config_.frame_ms);
  result.vad_enabled = vad_ != nullptr;
  result.voice_activity_state = voice_activity_state_.load();
  result.input_level_dbfs = static_cast<double>(input_level_millidbfs_.load()) / 1000.0;
  result.vad_frames_processed = vad_frames_processed_.load();
  result.vad_speech_frames = vad_speech_frames_.load();
  result.vad_speech_events = vad_speech_events_.load();
  result.vad_silence_events = vad_silence_events_.load();
  result.speech_segments_completed = speech_segments_completed_.load();
  result.speech_segments_truncated = speech_segments_truncated_.load();
  result.speech_segments_dropped = speech_segments_.DropCount();
  result.last_segment_duration_ms = last_segment_duration_ms_.load();
  result.asr_enabled = recognizer_ != nullptr;
  result.asr_segments_processed = asr_segments_processed_.load();
  result.transcripts_published = transcripts_published_.load();
  result.asr_errors = asr_errors_.load();
  result.modules = module_manager_.Status();
  if (capture_module_ != nullptr) {
    result.capture_state = capture_module_->capture_state();
    result.metrics = capture_module_->metrics();
    result.last_error = capture_module_->last_error();
  }
  return result;
}

void AudioService::StopCaptureLocked() {
  module_manager_.StopAll();
  vad_stop_.store(true);
  if (vad_worker_.joinable()) {
    vad_worker_.join();
  }
  asr_stop_.store(true);
  if (asr_worker_.joinable()) {
    asr_worker_.join();
  }
  transcript_changed_.notify_all();
}

AudioCaptureState AudioService::CaptureStateLocked() const {
  return capture_module_ == nullptr ? AudioCaptureState::kStopped
                                    : capture_module_->capture_state();
}

void AudioService::ProcessVoiceActivity() {
  while (true) {
    auto frame = capture_module_->TryPop();
    if (!frame.has_value()) {
      if (vad_stop_.load()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    const VoiceActivityResult result = vad_->Analyze(*frame);
    voice_activity_state_.store(result.state);
    input_level_millidbfs_.store(static_cast<std::int32_t>(result.level_dbfs * 1000.0));
    vad_frames_processed_.fetch_add(1U);
    if (result.state == VoiceActivityState::kSpeech) {
      vad_speech_frames_.fetch_add(1U);
    }
    if (result.state_changed) {
      if (result.state == VoiceActivityState::kSpeech) {
        vad_speech_events_.fetch_add(1U);
      } else {
        vad_silence_events_.fetch_add(1U);
      }
    }
    auto segment = segmenter_->Process(*frame, result);
    if (segment.has_value()) {
      PublishSpeechSegment(std::move(*segment));
    }
  }
  auto final_segment = segmenter_->Flush();
  if (final_segment.has_value()) {
    PublishSpeechSegment(std::move(*final_segment));
  }
}

void AudioService::ResetVadMetrics() {
  voice_activity_state_.store(VoiceActivityState::kSilence);
  input_level_millidbfs_.store(-120000);
  vad_frames_processed_.store(0);
  vad_speech_frames_.store(0);
  vad_speech_events_.store(0);
  vad_silence_events_.store(0);
  speech_segments_completed_.store(0);
  speech_segments_truncated_.store(0);
  last_segment_duration_ms_.store(0);
}

void AudioService::PublishSpeechSegment(SpeechSegment segment) {
  speech_segments_completed_.fetch_add(1U);
  if (segment.truncated) {
    speech_segments_truncated_.fetch_add(1U);
  }
  last_segment_duration_ms_.store(segment.DurationMs());
  speech_segments_.TryPush(std::move(segment));
}

void AudioService::ProcessSpeechSegments() {
  while (true) {
    auto segment = speech_segments_.TryPop();
    if (!segment.has_value()) {
      if (asr_stop_.load()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    asr_segments_processed_.fetch_add(1U);
    try {
      const auto result = recognizer_->Recognize(*segment);
      if (result.success) {
        PublishTranscript(*segment, result);
      } else {
        asr_errors_.fetch_add(1U);
      }
    } catch (const std::exception&) {
      asr_errors_.fetch_add(1U);
    }
  }
}

void AudioService::PublishTranscript(const SpeechSegment& segment,
                                     const voice::SpeechRecognitionResult& result) {
  voice::SpeechTranscript transcript;
  transcript.timestamp_ms =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  transcript.start_sequence = segment.start_sequence;
  transcript.end_sequence = segment.end_sequence;
  transcript.duration_ms = segment.DurationMs();
  transcript.truncated = segment.truncated;
  transcript.discontinuous = segment.discontinuous;
  transcript.text = result.text;
  transcript.provider = result.provider;
  transcript.confidence = result.confidence;
  {
    std::lock_guard<std::mutex> lock(transcript_mutex_);
    transcript.id = ++transcript_version_;
    transcript_history_.push_back(transcript);
    while (transcript_history_.size() > 32U) {
      transcript_history_.pop_front();
    }
  }
  transcripts_published_.fetch_add(1U);
  transcript_changed_.notify_all();
}

void AudioService::ResetAsrMetrics() {
  asr_segments_processed_.store(0);
  transcripts_published_.store(0);
  asr_errors_.store(0);
  std::lock_guard<std::mutex> lock(transcript_mutex_);
  transcript_history_.clear();
}

}  // namespace audio
}  // namespace cockpit
