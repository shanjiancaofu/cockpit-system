#include <cmath>
#include <iostream>
#include <string>

#include "cockpit/modules/audio/frames/audio_frame.h"
#include "cockpit/modules/audio/vad/speech_segment.h"
#include "cockpit/modules/voice/asr/plugin_speech_recognizer.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "expected valid and invalid ASR plugin paths\n";
    return 1;
  }

  bool success = true;
  std::string error;
  success &= Expect(
      cockpit::voice::PluginSpeechRecognizer::Load("relative/plugin.so", "", &error) == nullptr &&
          error.find("absolute .so path") != std::string::npos,
      "relative ASR plugin path was accepted");
  success &= Expect(cockpit::voice::PluginSpeechRecognizer::Load(argv[2], "", &error) == nullptr &&
                        error.find("invalid ASR plugin API") != std::string::npos,
                    "incompatible ASR plugin ABI was accepted");

  auto recognizer =
      cockpit::voice::PluginSpeechRecognizer::Load(argv[1], "/etc/cockpit/test-asr.yaml", &error);
  success &= Expect(recognizer != nullptr, "valid C ASR plugin was not loaded");
  if (recognizer == nullptr) {
    std::cerr << error << '\n';
    return 1;
  }

  cockpit::audio::SpeechSegment segment;
  segment.truncated = true;
  segment.discontinuous = true;
  segment.samples.assign(cockpit::audio::AudioFrame::kSampleCount * 2U, 100);
  const cockpit::voice::SpeechRecognitionResult result = recognizer->Recognize(segment);
  success &= Expect(result.success && result.provider == "test-plugin" &&
                        result.text == "plugin samples=640 flags=3" &&
                        std::fabs(result.confidence - 0.75F) < 0.0001F,
                    "ASR plugin result was not translated correctly");

  cockpit::audio::SpeechSegment empty_segment;
  const cockpit::voice::SpeechRecognitionResult empty_result = recognizer->Recognize(empty_segment);
  success &= Expect(!empty_result.success && empty_result.error == "speech segment is empty",
                    "empty speech segment was accepted by ASR plugin adapter");
  return success ? 0 : 1;
}
