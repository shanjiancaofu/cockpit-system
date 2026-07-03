#include "modules/audio/vad/energy_vad.h"

#include <algorithm>
#include <cmath>

namespace cockpit {
namespace audio {
namespace {

constexpr double kPcm16FullScale = 32768.0;
constexpr double kMinimumDbfs = -120.0;

}  // namespace

EnergyVad::EnergyVad(EnergyVadConfig config) : config_(config) {
}

VoiceActivityResult EnergyVad::Analyze(const AudioFrame& frame) {
  if (frame.HasFlag(AudioFrameFlag::kDiscontinuity)) {
    Reset();
  }

  const double level_dbfs = CalculateLevelDbfs(frame);
  const bool above_threshold = level_dbfs >= config_.speech_threshold_dbfs;
  bool state_changed = false;
  if (above_threshold) {
    ++consecutive_speech_frames_;
    consecutive_silence_frames_ = 0;
    if (state_ == VoiceActivityState::kSilence &&
        consecutive_speech_frames_ >= config_.speech_start_frames) {
      state_ = VoiceActivityState::kSpeech;
      state_changed = true;
    }
  } else {
    ++consecutive_silence_frames_;
    consecutive_speech_frames_ = 0;
    if (state_ == VoiceActivityState::kSpeech &&
        consecutive_silence_frames_ >= config_.speech_end_frames) {
      state_ = VoiceActivityState::kSilence;
      state_changed = true;
    }
  }
  return {state_, level_dbfs, state_changed};
}

void EnergyVad::Reset() {
  state_ = VoiceActivityState::kSilence;
  consecutive_speech_frames_ = 0;
  consecutive_silence_frames_ = 0;
}

double EnergyVad::CalculateLevelDbfs(const AudioFrame& frame) {
  double square_sum = 0.0;
  for (const std::int16_t sample : frame.samples()) {
    const double normalized = static_cast<double>(sample) / kPcm16FullScale;
    square_sum += normalized * normalized;
  }
  const double rms = std::sqrt(square_sum / static_cast<double>(frame.samples().size()));
  if (rms <= 0.0) {
    return kMinimumDbfs;
  }
  return std::max(kMinimumDbfs, 20.0 * std::log10(rms));
}

}  // namespace audio
}  // namespace cockpit
