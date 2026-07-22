#include "cockpit/modules/voice/asr/sherpa_onnx_speech_recognizer.h"

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include "cockpit/modules/audio/vad/speech_segment.h"
#include "cockpit/modules/audio/wav/wav_file.h"

int main() {
  cockpit::audio::PcmBuffer pcm;
  std::string error;
  if (!cockpit::audio::ReadPcm16Wav(SHERPA_ONNX_TEST_WAV_PATH, &pcm, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  if (pcm.format.sample_rate_hz != 16000 || pcm.format.channels != 1) {
    std::cerr << "SenseVoice test WAV must be 16 kHz mono PCM16\n";
    return 1;
  }

  cockpit::voice::SherpaOnnxRecognizerConfig config;
  config.model_path = SHERPA_ONNX_TEST_MODEL_PATH;
  config.language = "zh";
  config.threads = 2;
  const auto initialization_start = std::chrono::steady_clock::now();
  cockpit::voice::SherpaOnnxSpeechRecognizer recognizer(std::move(config));
  const auto initialization_end = std::chrono::steady_clock::now();
  if (!recognizer.IsReady()) {
    std::cerr << recognizer.initialization_error() << '\n';
    return 1;
  }

  cockpit::audio::SpeechSegment segment;
  segment.samples = std::move(pcm.samples);
  const auto recognition_start = std::chrono::steady_clock::now();
  const auto result = recognizer.Recognize(segment);
  const auto recognition_end = std::chrono::steady_clock::now();
  if (!result.success) {
    std::cerr << result.error << '\n';
    return 1;
  }
  if (result.provider != "sherpa_onnx_sense_voice" ||
      result.text.find("开放时间") == std::string::npos) {
    std::cerr << "unexpected SenseVoice transcript: " << result.text << '\n';
    return 1;
  }

  const auto initialization_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     initialization_end - initialization_start)
                                     .count();
  const auto recognition_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(recognition_end - recognition_start)
          .count();
  std::cout << result.text << '\n'
            << "initialization_ms=" << initialization_ms << '\n'
            << "recognition_ms=" << recognition_ms << '\n';
  return 0;
}
