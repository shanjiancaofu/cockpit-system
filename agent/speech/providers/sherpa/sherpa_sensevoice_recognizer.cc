#include "agent/speech/providers/sherpa/sherpa_sensevoice_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent/speech/providers/sherpa/sherpa_runtime_paths.h"
#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace voice {
namespace {

void RequireFile(const std::filesystem::path& path, const char* label) {
  if (!std::filesystem::is_regular_file(path)) {
    throw std::invalid_argument(std::string("Sherpa ASR required ") + label +
                                " is missing: " + path.string());
  }
}

class SherpaSenseVoiceRecognizer final : public SpeechRecognizer {
 public:
  SherpaSenseVoiceRecognizer() {
    const auto root =
        cockpit::agent::sherpa::ResolveAiRoot() / "models" / "asr" / "sensevoice-small-int8";
    const auto model = root / "model.int8.onnx";
    const auto tokens = root / "tokens.txt";
    RequireFile(model, "model");
    RequireFile(tokens, "tokens");

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = static_cast<int>(audio::AudioFrame::kSampleRateHz);
    config.feat_config.feature_dim = 80;
    config.model_config.sense_voice.model = model.c_str();
    config.model_config.sense_voice.language = "auto";
    config.model_config.sense_voice.use_itn = 1;
    config.model_config.tokens = tokens.c_str();
    config.model_config.num_threads = 1;
    config.model_config.provider = "cpu";
    config.decoding_method = "greedy_search";

    recognizer_ = SherpaOnnxCreateOfflineRecognizer(&config);
    if (recognizer_ == nullptr) {
      throw std::runtime_error("failed to initialize Sherpa SenseVoice provider");
    }
  }

  ~SherpaSenseVoiceRecognizer() override {
    if (recognizer_ != nullptr) {
      SherpaOnnxDestroyOfflineRecognizer(recognizer_);
    }
  }

  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment,
                                    std::chrono::steady_clock::time_point deadline) override {
    if (std::chrono::steady_clock::now() >= deadline) {
      return {false, {}, "sherpa-sensevoice", 0.0F, "speech recognition deadline exceeded"};
    }
    if (segment.samples.empty()) {
      return {false, {}, "sherpa-sensevoice", 0.0F, "speech segment is empty"};
    }
    const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer_);
    if (stream == nullptr) {
      return {false, {}, "sherpa-sensevoice", 0.0F, "failed to create Sherpa ASR stream"};
    }
    std::vector<float> samples;
    samples.reserve(segment.samples.size());
    for (const auto sample : segment.samples) {
      samples.push_back(static_cast<float>(sample) / 32768.0F);
    }
    SherpaOnnxAcceptWaveformOffline(stream, static_cast<int32_t>(audio::AudioFrame::kSampleRateHz),
                                    samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxDecodeOfflineStream(recognizer_, stream);
    const SherpaOnnxOfflineRecognizerResult* result = SherpaOnnxGetOfflineStreamResult(stream);
    SpeechRecognitionResult output;
    output.provider = "sherpa-sensevoice";
    if (result != nullptr && result->text != nullptr) {
      output.success = true;
      output.text = result->text;
      output.confidence = 1.0F;
    } else {
      output.success = false;
      output.error = "Sherpa SenseVoice returned no transcript";
    }
    if (result != nullptr) {
      SherpaOnnxDestroyOfflineRecognizerResult(result);
    }
    SherpaOnnxDestroyOfflineStream(stream);
    return output;
  }

  void Cancel() override {
  }

 private:
  const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
};

class LazySherpaSenseVoiceRecognizer final : public SpeechRecognizer {
 public:
  LazySherpaSenseVoiceRecognizer() {
    const auto root =
        cockpit::agent::sherpa::ResolveAiRoot() / "models" / "asr" / "sensevoice-small-int8";
    RequireFile(root / "model.int8.onnx", "model");
    RequireFile(root / "tokens.txt", "tokens");
  }

  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment,
                                    std::chrono::steady_clock::time_point deadline) override {
    cancelled_.store(false);
    SherpaSenseVoiceRecognizer* implementation = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (implementation_ == nullptr) {
        implementation_ = std::make_unique<SherpaSenseVoiceRecognizer>();
      }
      implementation = implementation_.get();
    }
    if (cancelled_.load()) {
      return {false, {}, "sherpa-sensevoice", 0.0F, "speech recognition cancelled"};
    }
    return implementation->Recognize(segment, deadline);
  }

  void Cancel() override {
    cancelled_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (implementation_ != nullptr) {
      implementation_->Cancel();
    }
  }

 private:
  std::mutex mutex_;
  std::unique_ptr<SherpaSenseVoiceRecognizer> implementation_;
  std::atomic_bool cancelled_{false};
};

}  // namespace

std::unique_ptr<SpeechRecognizer> CreateSherpaSenseVoiceRecognizer() {
  return std::make_unique<LazySherpaSenseVoiceRecognizer>();
}

}  // namespace voice
}  // namespace cockpit
