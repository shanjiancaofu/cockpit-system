#pragma once

#include <cstdint>

#include "cockpit/modules/audio/vad/voice_activity_detector.h"

namespace cockpit {
namespace audio {

struct EnergyVadConfig {
  double speech_threshold_dbfs = -40.0;
  std::uint32_t speech_start_frames = 3;
  std::uint32_t speech_end_frames = 10;
};

class EnergyVad final : public VoiceActivityDetector {
 public:
  explicit EnergyVad(EnergyVadConfig config);

  VoiceActivityResult Analyze(const AudioFrame& frame) override;
  void Reset() override;

 private:
  static double CalculateLevelDbfs(const AudioFrame& frame);

  const EnergyVadConfig config_;
  VoiceActivityState state_ = VoiceActivityState::kSilence;
  std::uint32_t consecutive_speech_frames_ = 0;
  std::uint32_t consecutive_silence_frames_ = 0;
};

}  // namespace audio
}  // namespace cockpit
