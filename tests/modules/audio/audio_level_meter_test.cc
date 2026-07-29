#include "cockpit/modules/audio/analysis/audio_level_meter.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

cockpit::audio::AudioFrame MakeFrame(std::int16_t sample) {
  cockpit::audio::AudioFrame::Samples samples{};
  samples.fill(sample);
  return cockpit::audio::AudioFrame(0, 0, cockpit::audio::AudioFrameFlag::kNone, samples);
}

}  // namespace

int main() {
  const auto silence = cockpit::audio::MeasureAudioLevel(MakeFrame(0));
  if (silence.rms_dbfs != -120.0 || silence.peak_dbfs != -120.0 || silence.clipped_samples != 0) {
    std::cerr << "silence audio level is invalid\n";
    return 1;
  }

  const auto nominal = cockpit::audio::MeasureAudioLevel(MakeFrame(10000));
  if (std::abs(nominal.rms_dbfs + 10.31) > 0.1 || std::abs(nominal.peak_dbfs + 10.31) > 0.1 ||
      nominal.clipped_samples != 0) {
    std::cerr << "nominal audio level is invalid\n";
    return 1;
  }

  const auto clipped = cockpit::audio::MeasureAudioLevel(MakeFrame(32767));
  if (clipped.clipped_samples != cockpit::audio::AudioFrame::kSampleCount) {
    std::cerr << "clipped samples were not counted\n";
    return 1;
  }
  return 0;
}
