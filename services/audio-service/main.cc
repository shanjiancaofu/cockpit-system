#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "core/logging/Logger.h"
#include "core/runtime/ServiceRuntime.h"
#include "drivers/alsa/alsa_audio_player.h"
#include "modules/voice/asr/mock_speech_recognizer.h"
#include "modules/voice/tts/mock_speech_synthesizer.h"
#include "services/audio-service/grpc/audio_grpc_service.h"
#include "services/audio-service/playback/speech_output.h"
#include "services/audio-service/processing/audio_service.h"

#if defined(COCKPIT_HAS_WHISPER_CPP_ASR)
#include "modules/voice/asr/whisper_speech_recognizer.h"
#endif

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "audio-service");
  std::unique_ptr<cockpit::voice::SpeechRecognizer> recognizer;
  if (runtime.config().features().voice.enabled) {
    const auto& voice_config = runtime.config().features().voice;
    if (voice_config.asr_provider == "mock") {
      recognizer = std::make_unique<cockpit::voice::MockSpeechRecognizer>();
    } else if (voice_config.asr_provider == "whisper_cpp") {
#if defined(COCKPIT_HAS_WHISPER_CPP_ASR)
      cockpit::voice::WhisperRecognizerConfig whisper_config;
      whisper_config.model_path = voice_config.asr_model_path;
      whisper_config.language = voice_config.asr_language;
      whisper_config.threads = voice_config.asr_threads;
      auto whisper_recognizer =
          std::make_unique<cockpit::voice::WhisperSpeechRecognizer>(std::move(whisper_config));
      if (!whisper_recognizer->IsReady()) {
        LOG_ERROR(whisper_recognizer->initialization_error());
        runtime.MarkStopped();
        return 1;
      }
      recognizer = std::move(whisper_recognizer);
#else
      LOG_ERROR("whisper_cpp ASR requested but BUILD_WHISPER_CPP_ASR is disabled");
      runtime.MarkStopped();
      return 1;
#endif
    }
  }
  cockpit::audio::AudioService audio_service(
      runtime.config().hardware().audio, runtime.config().services().audio.vad,
      runtime.config().services().audio.speech_segment, std::move(recognizer));
  cockpit::audio::SpeechOutput speech_output(
      runtime.args().GetString("output-device", runtime.config().hardware().audio.output_device),
      std::make_unique<cockpit::voice::MockSpeechSynthesizer>(),
      std::make_unique<cockpit::audio::AlsaAudioPlayer>());
  std::string output_error;
  if (!speech_output.Start(&output_error)) {
    LOG_ERROR("failed to start speech output: " + output_error);
    runtime.MarkStopped();
    return 1;
  }
  cockpit::audio::AudioGrpcService grpc_service(audio_service, speech_output);
  const auto& service_config = runtime.config().services().audio;
  if (!grpc_service.Start(service_config.grpc.listen_address)) {
    runtime.MarkStopped();
    return 1;
  }

  if (service_config.auto_start) {
    std::string error;
    if (!audio_service.StartCapture("", &error)) {
      LOG_ERROR("failed to auto-start audio capture: " + error);
      grpc_service.Shutdown();
      runtime.MarkStopped();
      return 1;
    }
  }

  while (!runtime.ShouldStop()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  grpc_service.Shutdown();
  audio_service.StopCapture();
  speech_output.Stop();
  runtime.MarkStopped();
  return 0;
}
