#pragma once

#include <cstdint>

#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace audio {

struct AudioLevel {
  double rms_dbfs = -120.0;
  double peak_dbfs = -120.0;
  std::uint32_t clipped_samples = 0;
};

AudioLevel MeasureAudioLevel(const AudioFrame& frame);

}  // namespace audio
}  // namespace cockpit
