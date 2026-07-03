#include "modules/voice/asr/whisper_speech_recognizer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "modules/audio/speech_segment.h"
#include "modules/audio/wav_file.h"

int main() {
  cockpit::audio::PcmBuffer pcm;
  std::string error;
  if (!cockpit::audio::ReadPcm16Wav(WHISPER_TEST_WAV_PATH, &pcm, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  if (pcm.format.sample_rate_hz != 16000 || pcm.format.channels != 1) {
    std::cerr << "Whisper test WAV must be 16 kHz mono PCM16\n";
    return 1;
  }

  cockpit::voice::WhisperRecognizerConfig config;
  config.model_path = WHISPER_TEST_MODEL_PATH;
  config.language = "en";
  config.threads = 4;
  cockpit::voice::WhisperSpeechRecognizer recognizer(std::move(config));
  if (!recognizer.IsReady()) {
    std::cerr << recognizer.initialization_error() << '\n';
    return 1;
  }

  cockpit::audio::SpeechSegment segment;
  segment.samples = std::move(pcm.samples);
  const auto result = recognizer.Recognize(segment);
  if (!result.success) {
    std::cerr << result.error << '\n';
    return 1;
  }

  std::string text = result.text;
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (text.find("country") == std::string::npos) {
    std::cerr << "unexpected Whisper transcript: " << result.text << '\n';
    return 1;
  }

  std::cout << result.text << '\n';
  return 0;
}
