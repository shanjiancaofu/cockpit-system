#include "agent/speech/providers/sherpa/sherpa_wake_word_detector.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cockpit {
namespace agent {
namespace {

std::string JoinPath(const std::string& root, const char* name) {
  return (std::filesystem::path(root) / name).string();
}

void RequireFile(const std::string& path, const std::string& label) {
  if (!std::filesystem::is_regular_file(path)) {
    throw std::invalid_argument("Sherpa KWS required " + label + " is missing: " + path);
  }
}

class SherpaWakeWordDetector final : public WakeWordDetector {
 public:
  explicit SherpaWakeWordDetector(config::KwsConfig config) : config_(std::move(config)) {
    if (config_.model_dir.empty()) {
      throw std::invalid_argument("Sherpa KWS model_dir is required");
    }
    if (config_.keywords_file.empty()) {
      throw std::invalid_argument("Sherpa KWS keywords_file is required");
    }
    if (!config_.wake_word.empty()) {
      throw std::invalid_argument(
          "Sherpa KWS requires a tokenized keywords_file; raw wake_word must be empty");
    }
    SherpaOnnxKeywordSpotterConfig sherpa_config;
    std::memset(&sherpa_config, 0, sizeof(sherpa_config));
    encoder_ = JoinPath(config_.model_dir, "encoder-epoch-13-avg-2-chunk-8-left-64.int8.onnx");
    decoder_ = JoinPath(config_.model_dir, "decoder-epoch-13-avg-2-chunk-8-left-64.onnx");
    joiner_ = JoinPath(config_.model_dir, "joiner-epoch-13-avg-2-chunk-8-left-64.int8.onnx");
    tokens_ = JoinPath(config_.model_dir, "tokens.txt");
    RequireFile(encoder_, "encoder model");
    RequireFile(decoder_, "decoder model");
    RequireFile(joiner_, "joiner model");
    RequireFile(tokens_, "tokens file");
    RequireFile(config_.keywords_file, "keywords file");
    sherpa_config.feat_config.sample_rate = static_cast<int>(audio::AudioFrame::kSampleRateHz);
    sherpa_config.feat_config.feature_dim = 80;
    sherpa_config.model_config.transducer.encoder = encoder_.c_str();
    sherpa_config.model_config.transducer.decoder = decoder_.c_str();
    sherpa_config.model_config.transducer.joiner = joiner_.c_str();
    sherpa_config.model_config.tokens = tokens_.c_str();
    sherpa_config.model_config.provider = "cpu";
    sherpa_config.model_config.num_threads = 1;
    sherpa_config.max_active_paths = 4;
    sherpa_config.num_trailing_blanks = 1;
    sherpa_config.keywords_score = 3.0F;
    sherpa_config.keywords_threshold = 0.1F;
    sherpa_config.keywords_file = config_.keywords_file.c_str();
    spotter_ = SherpaOnnxCreateKeywordSpotter(&sherpa_config);
    if (spotter_ == nullptr) {
      throw std::runtime_error("failed to initialize Sherpa KWS provider");
    }
    CreateStream();
  }

  ~SherpaWakeWordDetector() override {
    if (stream_ != nullptr) {
      SherpaOnnxDestroyOnlineStream(stream_);
    }
    if (spotter_ != nullptr) {
      SherpaOnnxDestroyKeywordSpotter(spotter_);
    }
  }

  WakeWordResult Analyze(const audio::AudioFrame& frame) override {
    if (stream_ == nullptr) {
      return {false, {}, "Sherpa KWS stream is not initialized"};
    }
    scratch_.resize(frame.samples().size());
    for (std::size_t index = 0; index < frame.samples().size(); ++index) {
      scratch_[index] = static_cast<float>(frame.samples()[index]) / 32768.0F;
    }
    SherpaOnnxOnlineStreamAcceptWaveform(stream_,
                                         static_cast<int>(audio::AudioFrame::kSampleRateHz),
                                         scratch_.data(), static_cast<int32_t>(scratch_.size()));
    while (SherpaOnnxIsKeywordStreamReady(spotter_, stream_)) {
      SherpaOnnxDecodeKeywordStream(spotter_, stream_);
      const SherpaOnnxKeywordResult* result = SherpaOnnxGetKeywordResult(spotter_, stream_);
      const bool detected =
          result != nullptr && result->keyword != nullptr && std::strlen(result->keyword) > 0U;
      std::string keyword = detected ? result->keyword : std::string{};
      SherpaOnnxDestroyKeywordResult(result);
      if (detected) {
        Reset();
        return {true, std::move(keyword), {}};
      }
    }
    return {};
  }

  void Reset() override {
    if (stream_ != nullptr) {
      SherpaOnnxResetKeywordStream(spotter_, stream_);
    }
  }

 private:
  void CreateStream() {
    stream_ = SherpaOnnxCreateKeywordStream(spotter_);
    if (stream_ == nullptr) {
      throw std::runtime_error("failed to create Sherpa KWS stream");
    }
  }

  config::KwsConfig config_;
  std::string encoder_;
  std::string decoder_;
  std::string joiner_;
  std::string tokens_;
  const SherpaOnnxKeywordSpotter* spotter_ = nullptr;
  const SherpaOnnxOnlineStream* stream_ = nullptr;
  std::vector<float> scratch_;
};

}  // namespace

std::unique_ptr<WakeWordDetector> CreateSherpaWakeWordDetector(const config::KwsConfig& config) {
  return std::make_unique<SherpaWakeWordDetector>(config);
}

}  // namespace agent
}  // namespace cockpit
