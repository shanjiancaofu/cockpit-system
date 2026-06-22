#include "audio_service.h"

#include "drivers/alsa/alsa_capture_source.h"

#include <chrono>
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
    : AudioService(std::move(config), DefaultSourceFactory()) {}

AudioService::AudioService(config::AudioConfig config, SourceFactory source_factory)
    : config_(std::move(config)), source_factory_(std::move(source_factory)) {}

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
  return true;
}

void AudioService::StopCapture() {
  std::lock_guard<std::mutex> lock(mutex_);
  StopCaptureLocked();
}

std::optional<AudioFrame> AudioService::TryPopFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  return capture_stream_ == nullptr ? std::nullopt : capture_stream_->TryPop();
}

AudioServiceStatus AudioService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioServiceStatus result;
  result.input_device = input_device_.empty() ? config_.input_device : input_device_;
  result.sample_rate_hz = static_cast<std::uint32_t>(config_.sample_rate_hz);
  result.channels = static_cast<std::uint32_t>(config_.channels);
  result.frame_ms = static_cast<std::uint32_t>(config_.frame_ms);
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
}

}  // namespace audio
}  // namespace cockpit
