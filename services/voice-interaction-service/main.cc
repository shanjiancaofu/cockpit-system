#include "audio_transcript_client.h"
#include "core/runtime/ServiceRuntime.h"
#include "modules/voice/mock_voice_assistant.h"
#include "voice_grpc_service.h"
#include "voice_interaction_service.h"

#include <chrono>
#include <memory>
#include <thread>

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(
      argc, argv, "voice-interaction-service");
  const bool enabled = runtime.config().features().voice.enabled;
  std::unique_ptr<cockpit::voice::VoiceAssistant> assistant;
  if (enabled) {
    assistant = std::make_unique<cockpit::voice::MockVoiceAssistant>();
  }
  cockpit::voice::VoiceInteractionService service(enabled,
                                                   std::move(assistant));
  cockpit::voice::VoiceGrpcService grpc_service(service);
  const auto& config = runtime.config().services().voice_interaction;
  if (!grpc_service.Start(config.grpc.listen_address)) {
    runtime.MarkStopped();
    return 1;
  }

  int result = 0;
  if (enabled) {
    cockpit::voice::AudioTranscriptClient client(
        config.audio_address, config.stream_timeout_ms, config.retry_delay_ms);
    result = client.Stream(
        [&service](const cockpit::voice::SpeechTranscript& transcript) {
          service.HandleTranscript(transcript);
        },
        [&runtime] { return !runtime.ShouldStop(); },
        [&service] { service.RecordUpstreamReconnect(); },
        [&service](const std::string& error) { service.SetUpstreamError(error); });
  } else {
    while (!runtime.ShouldStop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  grpc_service.Shutdown();
  runtime.MarkStopped();
  return result;
}
