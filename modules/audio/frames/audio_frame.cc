#include "modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace audio {

AudioFrame::AudioFrame(std::uint64_t sequence, std::int64_t capture_time_ns, AudioFrameFlag flags,
                       Samples samples)
    : sequence_(sequence), capture_time_ns_(capture_time_ns), flags_(flags), samples_(samples) {
}

bool AudioFrame::HasFlag(AudioFrameFlag flag) const {
  return (static_cast<std::uint32_t>(flags_) & static_cast<std::uint32_t>(flag)) != 0U;
}

}  // namespace audio
}  // namespace cockpit
