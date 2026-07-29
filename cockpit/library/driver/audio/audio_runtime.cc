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
#include "cockpit/modules/voice/asr/plugin_speech_recognizer.h"
#include "cockpit/modules/voice/tts/mock_speech_synthesizer.h"

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
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    std::unique_ptr<voice::SpeechRecognizer> recognizer;
    if (config.features().voice.enabled) {
      const config::AsrConfig& asr = config.features().voice.asr;
      if (asr.provider == "mock") {
        recognizer = std::make_unique<voice::MockSpeechRecognizer>();
      } else {
        std::string plugin_error;
        recognizer = voice::PluginSpeechRecognizer::Load(asr.plugin_path, asr.plugin_config_path,
                                                         &plugin_error);
        if (recognizer == nullptr) {
          LOG_ERROR(plugin_error);
          return false;
        }
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
