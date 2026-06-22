#include "audio_service.h"

#include "drivers/alsa/alsa_capture_source.h"
#include "modules/audio/energy_vad.h"

#include <chrono>
#include <exception>
#include <thread>
#include <utility>

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
    : AudioService(std::move(config), config::VadConfig{},
                   config::SpeechSegmentConfig{}, DefaultSourceFactory()) {}

AudioService::AudioService(config::AudioConfig config, SourceFactory source_factory)
    : AudioService(std::move(config), config::VadConfig{},
                   config::SpeechSegmentConfig{}, std::move(source_factory)) {}

AudioService::AudioService(config::AudioConfig config,
                           config::VadConfig vad_config)
    : AudioService(std::move(config), std::move(vad_config),
                   config::SpeechSegmentConfig{}, DefaultSourceFactory()) {}

AudioService::AudioService(config::AudioConfig config,
                           config::VadConfig vad_config,
                           SourceFactory source_factory)
    : AudioService(std::move(config), std::move(vad_config),
                   config::SpeechSegmentConfig{}, std::move(source_factory)) {}

AudioService::AudioService(config::AudioConfig config,
                           config::VadConfig vad_config,
                           config::SpeechSegmentConfig segment_config)
    : AudioService(std::move(config), std::move(vad_config),
                   std::move(segment_config), DefaultSourceFactory()) {}

AudioService::AudioService(config::AudioConfig config,
                           config::VadConfig vad_config,
                           config::SpeechSegmentConfig segment_config,
                           SourceFactory source_factory)
    : config_(std::move(config)),
      vad_config_(std::move(vad_config)),
      segment_config_(std::move(segment_config)),
      source_factory_(std::move(source_factory)) {
  if (vad_config_.enabled) {
    EnergyVadConfig energy_config;
    energy_config.speech_threshold_dbfs = vad_config_.speech_threshold_dbfs;
    energy_config.speech_start_frames =
        static_cast<std::uint32_t>(vad_config_.speech_start_frames);
    energy_config.speech_end_frames =
        static_cast<std::uint32_t>(vad_config_.speech_end_frames);
    vad_ = std::make_unique<EnergyVad>(energy_config);
    SpeechSegmenterConfig segmenter_config;
    segmenter_config.pre_roll_frames = static_cast<std::size_t>(
        segment_config_.pre_roll_ms / config_.frame_ms);
    segmenter_config.max_segment_frames = static_cast<std::size_t>(
        segment_config_.max_segment_ms / config_.frame_ms);
    segmenter_ = std::make_unique<SpeechSegmenter>(segmenter_config);
  }
}

AudioService::~AudioService() {
  StopCapture();
}

bool AudioService::StartCapture(const std::string& input_device, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (capture_stream_ != nullptr) {
    const AudioCaptureState state = capture_stream_->state();
    if (state != AudioCaptureState::kStopped && state != AudioCaptureState::kFaulted) {
      if (error != nullptr) {
        *error = "audio capture is already active";
      }
      return false;
    }
    StopCaptureLocked();
  }

  input_device_ = input_device.empty() ? config_.input_device : input_device;
  auto source = source_factory_(input_device_, ToPcmFormat(config_));
  if (source == nullptr) {
    if (error != nullptr) {
      *error = "audio capture source factory returned null";
    }
    return false;
  }
  capture_stream_ = std::make_unique<AudioCaptureStream>(std::move(source));
  if (!capture_stream_->Start(error)) {
    capture_stream_.reset();
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(1);
  while (capture_stream_->state() == AudioCaptureState::kStarting &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (capture_stream_->state() == AudioCaptureState::kFaulted) {
    if (error != nullptr) {
      *error = capture_stream_->last_error();
    }
    return false;
  }
  ResetVadMetrics();
  speech_segments_.Reset();
  if (vad_ != nullptr) {
    vad_->Reset();
    segmenter_->Reset();
    vad_stop_.store(false);
    try {
      vad_worker_ = std::thread(&AudioService::ProcessVoiceActivity, this);
    } catch (const std::exception& exception) {
      capture_stream_->Stop();
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
  return speech_segments_.TryPop();
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
  result.input_level_dbfs =
      static_cast<double>(input_level_millidbfs_.load()) / 1000.0;
  result.vad_frames_processed = vad_frames_processed_.load();
  result.vad_speech_frames = vad_speech_frames_.load();
  result.vad_speech_events = vad_speech_events_.load();
  result.vad_silence_events = vad_silence_events_.load();
  result.speech_segments_completed = speech_segments_completed_.load();
  result.speech_segments_truncated = speech_segments_truncated_.load();
  result.speech_segments_dropped = speech_segments_.DropCount();
  result.last_segment_duration_ms = last_segment_duration_ms_.load();
  if (capture_stream_ != nullptr) {
    result.capture_state = capture_stream_->state();
    result.metrics = capture_stream_->metrics();
    result.last_error = capture_stream_->last_error();
  }
  return result;
}

void AudioService::StopCaptureLocked() {
  if (capture_stream_ != nullptr) {
    capture_stream_->Stop();
  }
  vad_stop_.store(true);
  if (vad_worker_.joinable()) {
    vad_worker_.join();
  }
}

void AudioService::ProcessVoiceActivity() {
  while (true) {
    auto frame = capture_stream_->TryPop();
    if (!frame.has_value()) {
      if (vad_stop_.load()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    const VoiceActivityResult result = vad_->Analyze(*frame);
    voice_activity_state_.store(result.state);
    input_level_millidbfs_.store(
        static_cast<std::int32_t>(result.level_dbfs * 1000.0));
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

}  // namespace audio
}  // namespace cockpit
