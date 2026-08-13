#include "cockpit/modules/audio/playback/alsa_audio_player.h"

#include <algorithm>
#include <cstddef>

#include "cockpit/drivers/alsa/alsa_pcm.h"

namespace cockpit {
namespace audio {

bool AlsaAudioPlayer::Play(const std::string& device, const PcmBuffer& buffer,
                           const std::atomic_bool& stop_requested, std::string* error) {
  if (buffer.samples.empty() || buffer.FrameCount() == 0U) {
    if (error != nullptr) {
      *error = "playback PCM buffer is empty";
    }
    return false;
  }
  AlsaPcm pcm;
  const AlsaPcmFormat driver_format{buffer.format.sample_rate_hz, buffer.format.channels,
                                    buffer.format.frame_ms};
  if (!pcm.Open(device, PcmDirection::kPlayback, driver_format, error)) {
    return false;
  }
  std::size_t offset = 0;
  while (offset < buffer.FrameCount()) {
    if (stop_requested.load()) {
      if (error != nullptr) {
        *error = "playback stopped";
      }
      return false;
    }
    const std::size_t frames =
        std::min(buffer.format.FramesPerPeriod(), buffer.FrameCount() - offset);
    const auto* samples =
        buffer.samples.data() + offset * static_cast<std::size_t>(buffer.format.channels);
    if (!pcm.WriteFrames(samples, frames, error, &stop_requested)) {
      return false;
    }
    offset += frames;
  }
  return pcm.Drain(error, &stop_requested);
}

}  // namespace audio
}  // namespace cockpit
