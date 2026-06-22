#pragma once

#include "modules/audio/audio_frame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cockpit {
namespace audio {

struct SpeechSegment {
  std::uint64_t start_sequence = 0;
  std::uint64_t end_sequence = 0;
  std::int64_t start_time_ns = 0;
  std::int64_t end_time_ns = 0;
  bool truncated = false;
  bool discontinuous = false;
  std::vector<std::int16_t> samples;

  std::size_t FrameCount() const {
    return samples.size() / AudioFrame::kSampleCount;
  }

  std::uint64_t DurationMs() const {
    return static_cast<std::uint64_t>(FrameCount()) * AudioFrame::kFrameMs;
  }
};

}  // namespace audio
}  // namespace cockpit
