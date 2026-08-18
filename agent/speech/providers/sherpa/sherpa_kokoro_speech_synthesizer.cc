#include "agent/speech/providers/sherpa/sherpa_kokoro_speech_synthesizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "agent/speech/providers/sherpa/sherpa_runtime_paths.h"

namespace cockpit {
namespace voice {
namespace {

void RequireFile(const std::filesystem::path& path, const char* label) {
  if (!std::filesystem::is_regular_file(path)) {
    throw std::invalid_argument(std::string("Sherpa Kokoro required ") + label +
                                " is missing: " + path.string());
  }
}

std::filesystem::path KokoroRoot() {
  return cockpit::agent::sherpa::ResolveAiRoot() / "models" / "tts" / "kokoro-multi-lang-v1_1";
}

void ValidateKokoroResources() {
  const auto root = KokoroRoot();
  RequireFile(root / "model.onnx", "model");
  RequireFile(root / "voices.bin", "voices");
  RequireFile(root / "tokens.txt", "tokens");
  RequireFile(root / "lexicon-us-en.txt", "English lexicon");
  RequireFile(root / "lexicon-zh.txt", "Chinese lexicon");
  if (!std::filesystem::is_directory(root / "espeak-ng-data")) {
    throw std::invalid_argument("Sherpa Kokoro espeak-ng-data is missing: " +
                                (root / "espeak-ng-data").string());
  }
}

class SherpaKokoroSpeechSynthesizerImpl final : public SpeechSynthesizer {
 public:
  explicit SherpaKokoroSpeechSynthesizerImpl(const config::TtsConfig& config)
      : speaker_id_(config.speaker_id), speed_(static_cast<float>(config.speed)) {
    const auto root = KokoroRoot();
    const auto model = root / "model.onnx";
    const auto voices = root / "voices.bin";
    const auto tokens = root / "tokens.txt";
    const auto data_dir = root / "espeak-ng-data";
    const auto lexicon_en = root / "lexicon-us-en.txt";
    const auto lexicon_zh = root / "lexicon-zh.txt";
    RequireFile(model, "model");
    RequireFile(voices, "voices");
    RequireFile(tokens, "tokens");
    RequireFile(lexicon_en, "English lexicon");
    RequireFile(lexicon_zh, "Chinese lexicon");
    if (!std::filesystem::is_directory(data_dir)) {
      throw std::invalid_argument("Sherpa Kokoro espeak-ng-data is missing: " + data_dir.string());
    }
    const std::string lexicons = lexicon_en.string() + "," + lexicon_zh.string();

    SherpaOnnxOfflineTtsConfig sherpa_config;
    std::memset(&sherpa_config, 0, sizeof(sherpa_config));
    sherpa_config.model.kokoro.model = model.c_str();
    sherpa_config.model.kokoro.voices = voices.c_str();
    sherpa_config.model.kokoro.tokens = tokens.c_str();
    sherpa_config.model.kokoro.data_dir = data_dir.c_str();
    sherpa_config.model.kokoro.lexicon = lexicons.c_str();
    sherpa_config.model.kokoro.length_scale = 1.0F;
    sherpa_config.model.num_threads = 2;
    sherpa_config.model.provider = "cpu";
    sherpa_config.max_num_sentences = 1;
    sherpa_config.silence_scale = 0.2F;
    tts_ = SherpaOnnxCreateOfflineTts(&sherpa_config);
    if (tts_ == nullptr) {
      throw std::runtime_error("failed to initialize Sherpa Kokoro TTS provider");
    }
  }

  ~SherpaKokoroSpeechSynthesizerImpl() override {
    Cancel();
    if (tts_ != nullptr) {
      SherpaOnnxDestroyOfflineTts(tts_);
    }
  }

  SpeechSynthesisResult Synthesize(const std::string& text,
                                   std::chrono::steady_clock::time_point deadline) override {
    SpeechSynthesisResult result;
    result.provider = "sherpa-kokoro";
    if (text.empty()) {
      result.error = "TTS text is empty";
      return result;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      result.error = "speech synthesis deadline exceeded";
      return result;
    }

    cancelled_.store(false);
    CallbackContext callback{&cancelled_, deadline};
    SherpaOnnxGenerationConfig generation;
    std::memset(&generation, 0, sizeof(generation));
    generation.sid = speaker_id_;
    generation.speed = speed_;
    generation.silence_scale = 0.2F;
    const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(
        tts_, text.c_str(), &generation, &ContinueGeneration, &callback);
    if (audio == nullptr) {
      result.error = callback.ShouldContinue() ? "Sherpa Kokoro synthesis failed"
                                               : "speech synthesis cancelled or timed out";
      return result;
    }
    std::unique_ptr<const SherpaOnnxGeneratedAudio,
                    decltype(&SherpaOnnxDestroyOfflineTtsGeneratedAudio)>
        owned_audio(audio, &SherpaOnnxDestroyOfflineTtsGeneratedAudio);
    if (!callback.ShouldContinue()) {
      result.error = "speech synthesis cancelled or timed out";
      return result;
    }
    if (audio->samples == nullptr || audio->n <= 0 || audio->sample_rate <= 0) {
      result.error = "Sherpa Kokoro returned invalid audio";
      return result;
    }

    result.audio.format.sample_rate_hz = audio->sample_rate;
    result.audio.format.channels = 1;
    result.audio.format.frame_ms = 20;
    result.audio.samples.reserve(static_cast<std::size_t>(audio->n));
    for (int32_t index = 0; index < audio->n; ++index) {
      const float scaled = std::clamp(audio->samples[index], -1.0F, 1.0F) * 32767.0F;
      result.audio.samples.push_back(static_cast<std::int16_t>(std::lround(scaled)));
    }
    result.success = true;
    return result;
  }

  void Cancel() override {
    cancelled_.store(true);
  }

 private:
  struct CallbackContext {
    std::atomic_bool* cancelled;
    std::chrono::steady_clock::time_point deadline;

    bool ShouldContinue() const {
      return !cancelled->load() && std::chrono::steady_clock::now() < deadline;
    }
  };

  static int32_t ContinueGeneration(const float*, int32_t, float, void* argument) {
    return static_cast<CallbackContext*>(argument)->ShouldContinue() ? 1 : 0;
  }

  const SherpaOnnxOfflineTts* tts_ = nullptr;
  int speaker_id_ = 3;
  float speed_ = 1.0F;
  std::atomic_bool cancelled_{false};
};

class LazySherpaKokoroSpeechSynthesizer final : public SpeechSynthesizer {
 public:
  explicit LazySherpaKokoroSpeechSynthesizer(config::TtsConfig config)
      : config_(std::move(config)) {
    ValidateKokoroResources();
  }

  SpeechSynthesisResult Synthesize(const std::string& text,
                                   std::chrono::steady_clock::time_point deadline) override {
    cancelled_.store(false);
    SherpaKokoroSpeechSynthesizerImpl* implementation = nullptr;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (implementation_ == nullptr) {
        implementation_ = std::make_unique<SherpaKokoroSpeechSynthesizerImpl>(config_);
      }
      implementation = implementation_.get();
    } catch (const std::exception& exception) {
      return {false, {}, "sherpa-kokoro", exception.what()};
    }
    if (cancelled_.load()) {
      implementation->Cancel();
      return {false, {}, "sherpa-kokoro", "speech synthesis cancelled"};
    }
    return implementation->Synthesize(text, deadline);
  }

  void Cancel() override {
    cancelled_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (implementation_ != nullptr) {
      implementation_->Cancel();
    }
  }

 private:
  const config::TtsConfig config_;
  std::mutex mutex_;
  std::unique_ptr<SherpaKokoroSpeechSynthesizerImpl> implementation_;
  std::atomic_bool cancelled_{false};
};

}  // namespace

std::unique_ptr<SpeechSynthesizer> CreateSherpaKokoroSpeechSynthesizer(
    const config::TtsConfig& config) {
  return std::make_unique<LazySherpaKokoroSpeechSynthesizer>(config);
}

}  // namespace voice
}  // namespace cockpit
