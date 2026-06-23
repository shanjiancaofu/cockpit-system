#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "audio_grpc_service.h"
#include "audio_service.h"
#include "speech_output.h"

#include "core/logging/Logger.h"
#include "core/runtime/ServiceRuntime.h"
#include "drivers/alsa/alsa_audio_player.h"
#include "modules/voice/mock_speech_recognizer.h"
#include "modules/voice/mock_speech_synthesizer.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "audio-service");
  std::unique_ptr<cockpit::voice::SpeechRecognizer> recognizer;
  if (runtime.config().features().voice.enabled) {
    recognizer = std::make_unique<cockpit::voice::MockSpeechRecognizer>();
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
