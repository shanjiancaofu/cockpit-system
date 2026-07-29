#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "cockpit/modules/audio/vad/plugin_voice_activity_detector.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

cockpit::audio::AudioFrame MakeFrame(std::int16_t sample) {
  cockpit::audio::AudioFrame::Samples samples{};
  samples.fill(sample);
  return cockpit::audio::AudioFrame(1, 20000000, cockpit::audio::AudioFrameFlag::kDiscontinuity,
                                    samples);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "expected valid and invalid VAD plugin paths\n";
    return 1;
  }

  bool success = true;
  std::string error;
  success &= Expect(cockpit::audio::PluginVoiceActivityDetector::Load("relative/plugin.so", "",
                                                                      &error) == nullptr &&
                        error.find("absolute .so path") != std::string::npos,
                    "relative VAD plugin path was accepted");
  success &=
      Expect(cockpit::audio::PluginVoiceActivityDetector::Load(argv[2], "", &error) == nullptr &&
                 error.find("invalid VAD plugin API") != std::string::npos,
             "incompatible VAD plugin ABI was accepted");

  auto detector = cockpit::audio::PluginVoiceActivityDetector::Load(
      argv[1], "/etc/cockpit/test-vad.yaml", &error);
  success &= Expect(detector != nullptr, "valid C VAD plugin was not loaded");
  if (detector == nullptr) {
    std::cerr << error << '\n';
    return 1;
  }

  auto result = detector->Analyze(MakeFrame(10000));
  success &=
      Expect(result.state == cockpit::audio::VoiceActivityState::kSpeech && result.state_changed &&
                 std::fabs(result.speech_probability - 0.8F) < 0.0001F,
             "VAD plugin speech result was not translated correctly");
  result = detector->Analyze(MakeFrame(10000));
  success &= Expect(!result.state_changed, "VAD plugin repeated a state transition");
  detector->Reset();
  result = detector->Analyze(MakeFrame(0));
  success &=
      Expect(result.state == cockpit::audio::VoiceActivityState::kSilence &&
                 !result.state_changed && std::fabs(result.speech_probability - 0.1F) < 0.0001F,
             "VAD plugin silence result was not translated correctly");
  return success ? 0 : 1;
}
