#include "audio_grpc_service.h"
#include "audio_service.h"
#include "core/logging/Logger.h"
#include "core/runtime/ServiceRuntime.h"

#include <chrono>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "audio-service");
  cockpit::audio::AudioService audio_service(
      runtime.config().hardware().audio,
      runtime.config().services().audio.vad);
  cockpit::audio::AudioGrpcService grpc_service(audio_service);
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
  runtime.MarkStopped();
  return 0;
}
