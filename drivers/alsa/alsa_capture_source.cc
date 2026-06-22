#include "drivers/alsa/alsa_capture_source.h"

#include "modules/audio/audio_frame.h"

#include <utility>

namespace cockpit {
namespace audio {

AlsaCaptureSource::AlsaCaptureSource(std::string device, PcmFormat format)
    : device_(std::move(device)), format_(format) {}

bool AlsaCaptureSource::Open(std::string* error) {
  if (format_.sample_rate_hz != static_cast<int>(AudioFrame::kSampleRateHz) ||
      format_.channels != static_cast<int>(AudioFrame::kChannels) ||
      format_.frame_ms != static_cast<int>(AudioFrame::kFrameMs)) {
    if (error != nullptr) {
      *error = "audio capture stream requires 16000 Hz mono 20 ms PCM";
    }
    return false;
  }
  return pcm_.Open(device_, PcmDirection::kCapture, format_, error);
}

CaptureResult AlsaCaptureSource::Read(
    std::int16_t* samples, std::size_t frame_capacity, int timeout_ms,
    const std::atomic_bool& stop_requested) {
  return pcm_.PollReadFrames(samples, frame_capacity, timeout_ms,
                             stop_requested);
}

void AlsaCaptureSource::Close() {
  pcm_.Close();
}

}  // namespace audio
}  // namespace cockpit
