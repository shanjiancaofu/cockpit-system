#include "drivers/alsa/alsa_audio_player.h"

#include <algorithm>
#include <cstddef>

#include "drivers/alsa/alsa_pcm.h"

namespace cockpit {
namespace audio {

bool AlsaAudioPlayer::Play(const std::string& device, const PcmBuffer& buffer, std::string* error) {
  if (buffer.samples.empty() || buffer.FrameCount() == 0U) {
    if (error != nullptr) {
      *error = "playback PCM buffer is empty";
    }
    return false;
  }
  AlsaPcm pcm;
  if (!pcm.Open(device, PcmDirection::kPlayback, buffer.format, error)) {
    return false;
  }
  std::size_t offset = 0;
  while (offset < buffer.FrameCount()) {
    const std::size_t frames =
        std::min(buffer.format.FramesPerPeriod(), buffer.FrameCount() - offset);
    const auto* samples =
        buffer.samples.data() + offset * static_cast<std::size_t>(buffer.format.channels);
    if (!pcm.WriteFrames(samples, frames, error)) {
      return false;
    }
    offset += frames;
  }
  return pcm.Drain(error);
}

}  // namespace audio
}  // namespace cockpit
