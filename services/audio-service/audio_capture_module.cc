#include "services/audio-service/audio_capture_module.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace cockpit {
namespace audio {

AudioCaptureModule::AudioCaptureModule() : BasicModule("audio-capture") {
}

void AudioCaptureModule::Configure(std::unique_ptr<AudioCaptureStream> capture_stream) {
  capture_stream_ = std::move(capture_stream);
  last_error_.clear();
}

AudioCaptureState AudioCaptureModule::capture_state() const {
  return capture_stream_ == nullptr ? AudioCaptureState::kStopped : capture_stream_->state();
}

AudioCaptureMetrics AudioCaptureModule::metrics() const {
  return capture_stream_ == nullptr ? AudioCaptureMetrics{} : capture_stream_->metrics();
}

std::string AudioCaptureModule::last_error() const {
  if (capture_stream_ != nullptr && !capture_stream_->last_error().empty()) {
    return capture_stream_->last_error();
  }
  return last_error_;
}

std::optional<AudioFrame> AudioCaptureModule::TryPop() {
  if (capture_stream_ == nullptr) {
    return std::nullopt;
  }
  return capture_stream_->TryPop();
}

bool AudioCaptureModule::OnStart() {
  if (capture_stream_ == nullptr) {
    last_error_ = "audio capture stream is not configured";
    return false;
  }

  std::string error;
  if (!capture_stream_->Start(&error)) {
    last_error_ = error.empty() ? "start audio capture stream failed" : error;
    return false;
  }

  last_error_.clear();
  return true;
}

void AudioCaptureModule::OnStop() {
  if (capture_stream_ != nullptr) {
    capture_stream_->Stop();
  }
}

}  // namespace audio
}  // namespace cockpit
