#include "drivers/alsa/alsa_capture_source.h"

#include <chrono>
#include <thread>
#include <utility>

#include "modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace audio {

AlsaCaptureSource::AlsaCaptureSource(std::string device, PcmFormat format)
    : device_(std::move(device)), format_(format), pace_null_device_(device_ == "null") {
}

bool AlsaCaptureSource::Open(std::string* error) {
  if (format_.sample_rate_hz != static_cast<int>(AudioFrame::kSampleRateHz) ||
      format_.channels != static_cast<int>(AudioFrame::kChannels) ||
      format_.frame_ms != static_cast<int>(AudioFrame::kFrameMs)) {
    if (error != nullptr) {
      *error = "audio capture stream requires 16000 Hz mono 20 ms PCM";
    }
    return false;
  }
  if (!pcm_.Open(device_, PcmDirection::kCapture, format_, error)) {
    return false;
  }
  next_read_deadline_ = std::chrono::steady_clock::now();
  return true;
}

CaptureResult AlsaCaptureSource::Read(std::int16_t* samples, std::size_t frame_capacity,
                                      int timeout_ms, const std::atomic_bool& stop_requested) {
  CaptureResult result = pcm_.PollReadFrames(samples, frame_capacity, timeout_ms, stop_requested);
  if (result.status == CaptureStatus::kOk && result.frames_read > 0) {
    PaceNullDevice(result.frames_read);
  }
  return result;
}

void AlsaCaptureSource::Close() {
  pcm_.Close();
}

void AlsaCaptureSource::PaceNullDevice(std::size_t frames_read) {
  if (!pace_null_device_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (next_read_deadline_ < now) {
    next_read_deadline_ = now;
  }
  const auto duration = std::chrono::nanoseconds(static_cast<std::int64_t>(frames_read) *
                                                 1000000000LL / format_.sample_rate_hz);
  next_read_deadline_ += duration;
  std::this_thread::sleep_until(next_read_deadline_);
}

}  // namespace audio
}  // namespace cockpit
