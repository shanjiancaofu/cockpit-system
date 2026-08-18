#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "agent/speech/providers/sherpa/sherpa_kokoro_speech_synthesizer.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/modules/audio/wav/wav_file.h"

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "usage: " << argv[0] << " [output.wav]\n";
    return 2;
  }
  const std::string output = argc == 2 ? argv[1] : "/tmp/cockpit-kokoro-smoke.wav";
  cockpit::config::TtsConfig config;
  config.provider = "sherpa-kokoro";
  config.speaker_id = 3;
  config.speed = 1.0;
  try {
    auto synthesizer = cockpit::voice::CreateSherpaKokoroSpeechSynthesizer(config);
    const auto result =
        synthesizer->Synthesize("你好 我是座舱助手 Hello cockpit system",
                                std::chrono::steady_clock::now() + std::chrono::seconds(30));
    if (!result.success) {
      std::cerr << "Kokoro synthesis failed: " << result.error << '\n';
      return 1;
    }
    std::string error;
    if (!cockpit::audio::WritePcm16Wav(output, result.audio.format, result.audio.samples, &error)) {
      std::cerr << "failed to write TTS WAV: " << error << '\n';
      return 1;
    }
    std::cout << "Kokoro TTS generated " << result.audio.samples.size() << " samples at "
              << result.audio.format.sample_rate_hz << " Hz: " << output << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "Kokoro TTS setup failed: " << exception.what() << '\n';
    return 1;
  }
}
