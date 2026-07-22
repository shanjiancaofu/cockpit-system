#pragma once

#include <string>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/voice/asr/speech_recognizer.h"

struct SherpaOnnxOfflineRecognizer;

namespace cockpit {
namespace voice {

struct SherpaOnnxRecognizerConfig {
  std::string model_path;
  std::string tokens_path;
  std::string language = "zh";
  int threads = 2;
  bool use_inverse_text_normalization = true;
};

class SherpaOnnxSpeechRecognizer final : public SpeechRecognizer {
 public:
  explicit SherpaOnnxSpeechRecognizer(SherpaOnnxRecognizerConfig config);
  ~SherpaOnnxSpeechRecognizer() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(SherpaOnnxSpeechRecognizer);

  bool IsReady() const;
  const std::string& initialization_error() const;
  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment) override;

 private:
  SherpaOnnxRecognizerConfig config_;
  const SherpaOnnxOfflineRecognizer* recognizer_{nullptr};
  std::string initialization_error_;
};

}  // namespace voice
}  // namespace cockpit
