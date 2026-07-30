#include "agent/speech/tts/mock_speech_synthesizer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cockpit {
namespace voice {

SpeechSynthesisResult MockSpeechSynthesizer::Synthesize(const std::string& text) {
  SpeechSynthesisResult result;
  result.provider = "mock";
  if (text.empty()) {
    result.error = "TTS text is empty";
    return result;
  }

  result.audio.format.sample_rate_hz = 16000;
  result.audio.format.channels = 1;
  result.audio.format.frame_ms = 20;
  const std::size_t duration_ms = std::clamp<std::size_t>(text.size() * 20U, 200U, 1200U);
  const std::size_t sample_count =
      static_cast<std::size_t>(result.audio.format.sample_rate_hz) * duration_ms / 1000U;
  result.audio.samples.resize(sample_count);
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kFrequencyHz = 440.0;
  constexpr double kAmplitude = 2400.0;
  for (std::size_t index = 0; index < sample_count; ++index) {
    const double seconds = static_cast<double>(index) / result.audio.format.sample_rate_hz;
    result.audio.samples[index] =
        static_cast<std::int16_t>(kAmplitude * std::sin(2.0 * kPi * kFrequencyHz * seconds));
  }
  result.success = true;
  return result;
}

}  // namespace voice
}  // namespace cockpit
