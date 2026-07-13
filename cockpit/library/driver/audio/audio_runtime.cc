#include "cockpit/library/driver/audio/audio_runtime.h"

#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/drivers/alsa/alsa_audio_player.h"
#include "cockpit/library/driver/audio/grpc/audio_grpc_service.h"
#include "cockpit/library/driver/audio/playback/speech_output.h"
#include "cockpit/library/driver/audio/processing/audio_service.h"
#include "cockpit/modules/voice/asr/mock_speech_recognizer.h"
#include "cockpit/modules/voice/tts/mock_speech_synthesizer.h"

#if defined(COCKPIT_HAS_WHISPER_CPP_ASR)
#include "cockpit/modules/voice/asr/whisper_speech_recognizer.h"
#endif

namespace cockpit {
namespace audio {

class AudioRuntime::Impl {
 public:
  std::unique_ptr<AudioService> service;
  std::unique_ptr<SpeechOutput> speech_output;
  std::unique_ptr<AudioGrpcService> grpc;
};

AudioRuntime::AudioRuntime() = default;

AudioRuntime::~AudioRuntime() {
  Stop();
}

bool AudioRuntime::Start(const std::string& config_path,
                         const std::string& output_device_override) {
  if (impl_ != nullptr) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("audio_driver", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().max_bytes,
                        config.logging().mirror_stderr);
    std::unique_ptr<voice::SpeechRecognizer> recognizer;
    if (config.features().voice.enabled) {
      const auto& voice_config = config.features().voice;
      if (voice_config.asr_provider == "mock") {
        recognizer = std::make_unique<voice::MockSpeechRecognizer>();
      } else if (voice_config.asr_provider == "whisper_cpp") {
#if defined(COCKPIT_HAS_WHISPER_CPP_ASR)
        voice::WhisperRecognizerConfig whisper_config;
        whisper_config.model_path = voice_config.asr_model_path;
        whisper_config.language = voice_config.asr_language;
        whisper_config.threads = voice_config.asr_threads;
        auto whisper_recognizer =
            std::make_unique<voice::WhisperSpeechRecognizer>(std::move(whisper_config));
        if (!whisper_recognizer->IsReady()) {
          LOG_ERROR(whisper_recognizer->initialization_error());
          return false;
        }
        recognizer = std::move(whisper_recognizer);
#else
        LOG_ERROR("whisper_cpp ASR requested but BUILD_WHISPER_CPP_ASR is disabled");
        return false;
#endif
      }
    }

    impl_ = std::make_unique<Impl>();
    impl_->service = std::make_unique<AudioService>(
        config.hardware().audio, config.services().audio.vad,
        config.services().audio.speech_segment, std::move(recognizer));
    const std::string output_device = output_device_override.empty()
                                          ? config.hardware().audio.output_device
                                          : output_device_override;
    impl_->speech_output = std::make_unique<SpeechOutput>(
        output_device, std::make_unique<voice::MockSpeechSynthesizer>(),
        std::make_unique<AlsaAudioPlayer>());
    std::string error;
    if (!impl_->speech_output->Start(&error)) {
      LOG_ERROR("failed to start speech output: " + error);
      impl_.reset();
      return false;
    }
    impl_->grpc = std::make_unique<AudioGrpcService>(*impl_->service, *impl_->speech_output);
    if (!impl_->grpc->Start(config.services().audio.grpc.listen_address)) {
      impl_.reset();
      return false;
    }
    if (config.services().audio.auto_start && !impl_->service->StartCapture("", &error)) {
      LOG_ERROR("failed to auto-start audio capture: " + error);
      impl_.reset();
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure audio driver: " + std::string(error.what()));
    impl_.reset();
    return false;
  }
}

void AudioRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->grpc->Shutdown();
  impl_->service->StopCapture();
  impl_->speech_output->Stop();
  impl_.reset();
}

int AudioRuntime::Poll() const {
  return impl_ == nullptr ? 1 : 0;
}

}  // namespace audio
}  // namespace cockpit
