#include "agent/speech/providers/sherpa/sherpa_voice_activity_detector.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent/speech/providers/sherpa/sherpa_runtime_paths.h"

namespace cockpit {
namespace agent {
namespace {

void RequireFile(const std::filesystem::path& path, const char* label) {
  if (!std::filesystem::is_regular_file(path)) {
    throw std::invalid_argument(std::string("Sherpa VAD required ") + label +
                                " is missing: " + path.string());
  }
}

class SherpaVoiceActivityDetector final : public audio::VoiceActivityDetector {
 public:
  SherpaVoiceActivityDetector() {
    const auto root = sherpa::ResolveAiRoot() / "models" / "vad" / "silero-vad";
    const auto model = root / "silero_vad.onnx";
    RequireFile(model, "model");

    SherpaOnnxVadModelConfig config;
    std::memset(&config, 0, sizeof(config));
    config.silero_vad.model = model.c_str();
    config.silero_vad.threshold = 0.5F;
    config.silero_vad.min_silence_duration = 0.5F;
    config.silero_vad.min_speech_duration = 0.1F;
    config.silero_vad.window_size = 512;
    config.silero_vad.max_speech_duration = 20.0F;
    config.sample_rate = static_cast<int>(audio::AudioFrame::kSampleRateHz);
    config.num_threads = 1;
    config.provider = "cpu";
    config.debug = 0;

    detector_ = SherpaOnnxCreateVoiceActivityDetector(&config, 20.0F);
    if (detector_ == nullptr) {
      throw std::runtime_error("failed to initialize Sherpa VAD provider");
    }
  }

  ~SherpaVoiceActivityDetector() override {
    if (detector_ != nullptr) {
      SherpaOnnxDestroyVoiceActivityDetector(detector_);
    }
  }

  audio::VoiceActivityResult Analyze(const audio::AudioFrame& frame) override {
    scratch_.resize(frame.samples().size());
    for (std::size_t index = 0; index < frame.samples().size(); ++index) {
      scratch_[index] = static_cast<float>(frame.samples()[index]) / 32768.0F;
    }
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(detector_, scratch_.data(),
                                                  static_cast<int32_t>(scratch_.size()));
    const bool detected = SherpaOnnxVoiceActivityDetectorDetected(detector_) != 0;
    const bool changed = detected != detected_;
    detected_ = detected;
    return {detected ? audio::VoiceActivityState::kSpeech : audio::VoiceActivityState::kSilence,
            detected ? 1.0F : 0.0F, changed};
  }

  void Reset() override {
    SherpaOnnxVoiceActivityDetectorReset(detector_);
    detected_ = false;
    scratch_.clear();
  }

 private:
  const SherpaOnnxVoiceActivityDetector* detector_ = nullptr;
  std::vector<float> scratch_;
  bool detected_ = false;
};

}  // namespace

std::unique_ptr<audio::VoiceActivityDetector> CreateSherpaVoiceActivityDetector() {
  return std::make_unique<SherpaVoiceActivityDetector>();
}

}  // namespace agent
}  // namespace cockpit
