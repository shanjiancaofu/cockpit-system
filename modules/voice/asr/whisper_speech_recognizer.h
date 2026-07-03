#pragma once

#include <string>

#include "modules/voice/asr/speech_recognizer.h"

struct whisper_context;

namespace cockpit {
namespace voice {

struct WhisperRecognizerConfig {
  std::string model_path;
  std::string language = "zh";
  int threads = 4;
};

class WhisperSpeechRecognizer final : public SpeechRecognizer {
 public:
  explicit WhisperSpeechRecognizer(WhisperRecognizerConfig config);
  ~WhisperSpeechRecognizer() override;

  WhisperSpeechRecognizer(const WhisperSpeechRecognizer&) = delete;
  WhisperSpeechRecognizer& operator=(const WhisperSpeechRecognizer&) = delete;

  bool IsReady() const;
  const std::string& initialization_error() const;
  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment) override;

 private:
  WhisperRecognizerConfig config_;
  whisper_context* context_{nullptr};
  std::string initialization_error_;
};

}  // namespace voice
}  // namespace cockpit
