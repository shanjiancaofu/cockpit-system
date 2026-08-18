#include "cockpit/library/driver/audio/capture/audio_capture_controller.h"

#include <chrono>
#include <exception>
#include <string_view>
#include <thread>
#include <utility>

#include "cockpit/modules/audio/analysis/audio_level_meter.h"
#include "cockpit/modules/audio/capture/alsa_capture_source.h"
#include "cockpit/modules/audio/capture/audio_capture_stream.h"
#include "cockpit/modules/audio/capture/wav_capture_source.h"

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

AudioCaptureController::SourceFactory DefaultSourceFactory() {
  return [](const std::string& device, const PcmFormat& format) {
    constexpr std::string_view kWavPrefix = "wav:";
    if (device.rfind(kWavPrefix, 0) == 0) {
      return std::unique_ptr<AudioCaptureSource>(
          std::make_unique<WavCaptureSource>(device.substr(kWavPrefix.size()), format));
    }
    return std::unique_ptr<AudioCaptureSource>(std::make_unique<AlsaCaptureSource>(device, format));
  };
}

}  // namespace

AudioCaptureController::AudioCaptureController(config::AudioConfig config,
                                               AudioStreamPublisher& stream_publisher)
    : AudioCaptureController(std::move(config), DefaultSourceFactory(), stream_publisher) {
}

AudioCaptureController::AudioCaptureController(config::AudioConfig config,
                                               SourceFactory source_factory,
                                               AudioStreamPublisher& stream_publisher)
    : config_(std::move(config)),
      source_factory_(std::move(source_factory)),
      stream_publisher_(stream_publisher) {
  auto capture_module = std::make_unique<AudioCaptureModule>();
  capture_module_ = capture_module.get();
  module_manager_.Add(std::move(capture_module));
}

AudioCaptureController::~AudioCaptureController() {
  Stop();
}

bool AudioCaptureController::Start(const std::string& input_device, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const AudioCaptureState state = CaptureStateLocked();
  if (state != AudioCaptureState::kStopped && state != AudioCaptureState::kFaulted) {
    if (error != nullptr) {
      *error = "audio capture is already active";
    }
    return false;
  }
  StopLocked();
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
  const AudioCaptureState ready_state = capture_module_->capture_state();
  if (ready_state != AudioCaptureState::kRunning) {
    if (error != nullptr) {
      *error = ready_state == AudioCaptureState::kFaulted
                   ? capture_module_->last_error()
                   : "audio capture did not become ready before timeout";
    }
    StopLocked();
    return false;
  }
  input_level_millidbfs_.store(-120000);
  processing_stop_.store(false);
  try {
    processing_worker_ = std::thread(&AudioCaptureController::ProcessFrames, this);
  } catch (const std::exception& exception) {
    module_manager_.StopAll();
    if (error != nullptr) {
      *error = exception.what();
    }
    return false;
  }
  return true;
}

void AudioCaptureController::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  StopLocked();
}

AudioCaptureControllerStatus AudioCaptureController::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioCaptureControllerStatus result;
  result.input_device = input_device_.empty() ? config_.input_device : input_device_;
  result.sample_rate_hz = static_cast<std::uint32_t>(config_.sample_rate_hz);
  result.channels = static_cast<std::uint32_t>(config_.channels);
  result.frame_ms = static_cast<std::uint32_t>(config_.frame_ms);
  result.stream_metrics = stream_publisher_.metrics();
  result.input_level_dbfs = static_cast<double>(input_level_millidbfs_.load()) / 1000.0;
  result.modules = module_manager_.Status();
  if (capture_module_ != nullptr) {
    result.capture_state = capture_module_->capture_state();
    result.capture_metrics = capture_module_->metrics();
    result.last_error = capture_module_->last_error();
  }
  return result;
}

bool AudioCaptureController::faulted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return CaptureStateLocked() == AudioCaptureState::kFaulted;
}

void AudioCaptureController::StopLocked() {
  module_manager_.StopAll();
  processing_stop_.store(true);
  if (processing_worker_.joinable()) {
    processing_worker_.join();
  }
}

AudioCaptureState AudioCaptureController::CaptureStateLocked() const {
  return capture_module_ == nullptr ? AudioCaptureState::kStopped
                                    : capture_module_->capture_state();
}

void AudioCaptureController::ProcessFrames() {
  while (true) {
    auto frame = capture_module_->TryPop();
    if (!frame.has_value()) {
      if (processing_stop_.load()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    const AudioLevel level = MeasureAudioLevel(*frame);
    input_level_millidbfs_.store(static_cast<std::int32_t>(level.rms_dbfs * 1000.0));
    stream_publisher_.Publish(*frame);
  }
}

}  // namespace audio
}  // namespace cockpit
