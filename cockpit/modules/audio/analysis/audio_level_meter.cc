#include "cockpit/modules/audio/analysis/audio_level_meter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cockpit {
namespace audio {
namespace {

constexpr double kPcm16FullScale = 32768.0;
constexpr double kMinimumDbfs = -120.0;
constexpr std::int16_t kClipThreshold = 32760;

double ToDbfs(double amplitude) {
  if (amplitude <= 0.0) {
    return kMinimumDbfs;
  }
  return std::max(kMinimumDbfs, 20.0 * std::log10(amplitude));
}

}  // namespace

AudioLevel MeasureAudioLevel(const AudioFrame& frame) {
  double square_sum = 0.0;
  double peak = 0.0;
  std::uint32_t clipped_samples = 0;
  for (const std::int16_t sample : frame.samples()) {
    const double normalized = static_cast<double>(sample) / kPcm16FullScale;
    square_sum += normalized * normalized;
    peak = std::max(peak, std::abs(normalized));
    if (sample >= kClipThreshold || sample <= -kClipThreshold) {
      ++clipped_samples;
    }
  }
  const double rms = std::sqrt(square_sum / static_cast<double>(frame.samples().size()));
  return {ToDbfs(rms), ToDbfs(peak), clipped_samples};
}

}  // namespace audio
}  // namespace cockpit
