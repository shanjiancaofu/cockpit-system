#include "cockpit/modules/voice/asr/sherpa_onnx_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace cockpit {
namespace voice {
namespace {

constexpr int kSampleRateHz = 16000;
constexpr int kFeatureDimension = 80;
constexpr const char* kProviderName = "sherpa_onnx_sense_voice";

}  // namespace

SherpaOnnxSpeechRecognizer::SherpaOnnxSpeechRecognizer(SherpaOnnxRecognizerConfig config)
    : config_(std::move(config)) {
  const std::filesystem::path model_path(config_.model_path);
  if (!std::filesystem::is_regular_file(model_path)) {
    initialization_error_ = "SenseVoice model does not exist: " + config_.model_path;
    return;
  }
  if (config_.tokens_path.empty()) {
    config_.tokens_path = (model_path.parent_path() / "tokens.txt").string();
  }
  if (!std::filesystem::is_regular_file(config_.tokens_path)) {
    initialization_error_ = "SenseVoice tokens do not exist: " + config_.tokens_path;
    return;
  }

  SherpaOnnxOfflineSenseVoiceModelConfig sense_voice_config;
  std::memset(&sense_voice_config, 0, sizeof(sense_voice_config));
  sense_voice_config.model = config_.model_path.c_str();
  sense_voice_config.language = config_.language.c_str();
  sense_voice_config.use_itn = config_.use_inverse_text_normalization ? 1 : 0;

  SherpaOnnxOfflineModelConfig model_config;
  std::memset(&model_config, 0, sizeof(model_config));
  model_config.tokens = config_.tokens_path.c_str();
  model_config.num_threads = config_.threads;
  model_config.provider = "cpu";
  model_config.sense_voice = sense_voice_config;

  SherpaOnnxOfflineRecognizerConfig recognizer_config;
  std::memset(&recognizer_config, 0, sizeof(recognizer_config));
  recognizer_config.feat_config.sample_rate = kSampleRateHz;
  recognizer_config.feat_config.feature_dim = kFeatureDimension;
  recognizer_config.decoding_method = "greedy_search";
  recognizer_config.model_config = model_config;

  recognizer_ = SherpaOnnxCreateOfflineRecognizer(&recognizer_config);
  if (recognizer_ == nullptr) {
    initialization_error_ = "failed to load SenseVoice model: " + config_.model_path;
  }
}

SherpaOnnxSpeechRecognizer::~SherpaOnnxSpeechRecognizer() {
  if (recognizer_ != nullptr) {
    SherpaOnnxDestroyOfflineRecognizer(recognizer_);
  }
}

bool SherpaOnnxSpeechRecognizer::IsReady() const {
  return recognizer_ != nullptr;
}

const std::string& SherpaOnnxSpeechRecognizer::initialization_error() const {
  return initialization_error_;
}

SpeechRecognitionResult SherpaOnnxSpeechRecognizer::Recognize(const audio::SpeechSegment& segment) {
  if (recognizer_ == nullptr) {
    return {false, {}, kProviderName, 0.0F, initialization_error_};
  }
  if (segment.samples.empty()) {
    return {false, {}, kProviderName, 0.0F, "speech segment is empty"};
  }

  std::vector<float> pcm;
  pcm.reserve(segment.samples.size());
  for (const std::int16_t sample : segment.samples) {
    pcm.push_back(static_cast<float>(sample) / 32768.0F);
  }

  const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer_);
  if (stream == nullptr) {
    return {false, {}, kProviderName, 0.0F, "failed to create sherpa-onnx stream"};
  }
  SherpaOnnxAcceptWaveformOffline(stream, kSampleRateHz, pcm.data(),
                                  static_cast<std::int32_t>(pcm.size()));
  SherpaOnnxDecodeOfflineStream(recognizer_, stream);
  const SherpaOnnxOfflineRecognizerResult* result = SherpaOnnxGetOfflineStreamResult(stream);

  std::string text;
  if (result != nullptr && result->text != nullptr) {
    text = result->text;
  }
  if (result != nullptr) {
    SherpaOnnxDestroyOfflineRecognizerResult(result);
  }
  SherpaOnnxDestroyOfflineStream(stream);

  if (text.empty()) {
    return {false, {}, kProviderName, 0.0F, "sherpa-onnx returned empty text"};
  }
  return {true, std::move(text), kProviderName, 0.0F, {}};
}

}  // namespace voice
}  // namespace cockpit
