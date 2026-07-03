#include <chrono>
#include <memory>
#include <thread>

#include "core/runtime/ServiceRuntime.h"
#include "modules/voice/actions/cockpit_action_dispatcher.h"
#include "modules/voice/assistant/mock_voice_assistant.h"
#include "modules/voice/responses/async_voice_response_sink.h"
#include "services/voice-interaction-service/audio/audio_speech_client.h"
#include "services/voice-interaction-service/audio/audio_transcript_client.h"
#include "services/voice-interaction-service/grpc/voice_grpc_service.h"
#include "services/voice-interaction-service/hmi/local_hmi_command_provider.h"
#include "services/voice-interaction-service/interaction/voice_interaction_service.h"
#include "services/voice-interaction-service/vehicle/gateway_vehicle_status_client.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "voice-interaction-service");
  const bool enabled =
      runtime.config().features().voice.enabled || runtime.args().HasFlag("enable");
  std::unique_ptr<cockpit::voice::VoiceAssistant> assistant;
  std::unique_ptr<cockpit::voice::ActionDispatcher> dispatcher;
  std::unique_ptr<cockpit::voice::VoiceResponseSink> output;
  if (enabled) {
    assistant = std::make_unique<cockpit::voice::MockVoiceAssistant>();
    dispatcher = std::make_unique<cockpit::voice::CockpitActionDispatcher>(
        std::make_unique<cockpit::voice::GatewayVehicleStatusClient>(
            runtime.config().services().voice_interaction.gateway_address),
        std::make_unique<cockpit::voice::LocalHmiCommandProvider>());
    output = std::make_unique<cockpit::voice::AsyncVoiceResponseSink>(
        std::make_unique<cockpit::voice::AudioSpeechClient>(
            runtime.config().services().voice_interaction.audio_address));
  }
  cockpit::voice::VoiceInteractionService service(enabled, std::move(assistant),
                                                  std::move(dispatcher), std::move(output));
  cockpit::voice::VoiceGrpcService grpc_service(service);
  const auto& config = runtime.config().services().voice_interaction;
  if (!grpc_service.Start(config.grpc.listen_address)) {
    runtime.MarkStopped();
    return 1;
  }

  int result = 0;
  if (enabled) {
    service.Start();
    cockpit::voice::AudioTranscriptClient client(config.audio_address, config.stream_timeout_ms,
                                                 config.retry_delay_ms);
    result = client.Stream(
        [&service](const cockpit::voice::SpeechTranscript& transcript) {
          service.SubmitTranscript(transcript);
        },
        [&runtime] {
          return !runtime.ShouldStop();
        },
        [&service] {
          service.RecordUpstreamReconnect();
        },
        [&service](const std::string& error) {
          service.SetUpstreamError(error);
        });
  } else {
    while (!runtime.ShouldStop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  service.Stop();
  grpc_service.Shutdown();
  runtime.MarkStopped();
  return result;
}
