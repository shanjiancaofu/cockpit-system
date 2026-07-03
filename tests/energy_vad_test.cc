#include "modules/audio/vad/energy_vad.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

cockpit::audio::AudioFrame MakeFrame(
    std::uint64_t sequence, std::int16_t amplitude,
    cockpit::audio::AudioFrameFlag flags = cockpit::audio::AudioFrameFlag::kNone) {
  cockpit::audio::AudioFrame::Samples samples{};
  samples.fill(amplitude);
  return cockpit::audio::AudioFrame(sequence, static_cast<std::int64_t>(sequence), flags, samples);
}

}  // namespace

int main() {
  cockpit::audio::EnergyVadConfig config;
  config.speech_threshold_dbfs = -40.0;
  config.speech_start_frames = 2;
  config.speech_end_frames = 3;
  cockpit::audio::EnergyVad vad(config);

  auto result = vad.Analyze(MakeFrame(0, 0));
  if (result.state != cockpit::audio::VoiceActivityState::kSilence || result.level_dbfs != -120.0 ||
      result.state_changed) {
    std::cerr << "silence frame was classified incorrectly\n";
    return 1;
  }

  result = vad.Analyze(MakeFrame(1, 10000));
  if (result.state != cockpit::audio::VoiceActivityState::kSilence || result.state_changed ||
      std::abs(result.level_dbfs + 10.31) > 0.1) {
    std::cerr << "speech start debounce was not applied\n";
    return 1;
  }
  result = vad.Analyze(MakeFrame(2, 10000));
  if (result.state != cockpit::audio::VoiceActivityState::kSpeech || !result.state_changed) {
    std::cerr << "speech transition was not detected\n";
    return 1;
  }

  result = vad.Analyze(MakeFrame(3, 0));
  result = vad.Analyze(MakeFrame(4, 0));
  if (result.state != cockpit::audio::VoiceActivityState::kSpeech) {
    std::cerr << "speech end hangover ended too early\n";
    return 1;
  }
  result = vad.Analyze(MakeFrame(5, 0));
  if (result.state != cockpit::audio::VoiceActivityState::kSilence || !result.state_changed) {
    std::cerr << "silence transition was not detected\n";
    return 1;
  }

  result = vad.Analyze(MakeFrame(6, 10000, cockpit::audio::AudioFrameFlag::kDiscontinuity));
  if (result.state != cockpit::audio::VoiceActivityState::kSilence || result.state_changed) {
    std::cerr << "discontinuity did not reset VAD debounce state\n";
    return 1;
  }
  std::cout << "energy VAD tests passed\n";
  return 0;
}
