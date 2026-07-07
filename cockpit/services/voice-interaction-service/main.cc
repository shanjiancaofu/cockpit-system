#include <chrono>
#include <memory>
#include <sstream>
#include <thread>

#include "cockpit/core/runtime/ServiceRuntime.h"
#include "cockpit/modules/voice/actions/cockpit_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"
#include "cockpit/modules/voice/responses/async_voice_response_sink.h"
#include "cockpit/services/recording-service/client/recording_event_publisher.h"
#include "cockpit/services/voice-interaction-service/audio/audio_speech_client.h"
#include "cockpit/services/voice-interaction-service/audio/audio_transcript_client.h"
#include "cockpit/services/voice-interaction-service/grpc/voice_grpc_service.h"
#include "cockpit/services/voice-interaction-service/hmi/local_hmi_command_provider.h"
#include "cockpit/services/voice-interaction-service/interaction/voice_interaction_service.h"
#include "cockpit/services/voice-interaction-service/vehicle/gateway_vehicle_status_client.h"

namespace {

std::string EscapeJson(const std::string& input) {
  std::ostringstream output;
  for (const char character : input) {
    switch (character) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      default:
        output << character;
        break;
    }
  }
  return output.str();
}

std::string VoiceResponsePayload(const cockpit::voice::VoiceResponse& response) {
  std::ostringstream output;
  output << "{"
         << "\"id\":" << response.id << ',' << "\"transcript_id\":" << response.transcript_id << ','
         << "\"transcript_text\":\"" << EscapeJson(response.transcript_text) << "\","
         << "\"intent\":\"" << cockpit::voice::ToString(response.intent) << "\","
         << "\"action\":\"" << cockpit::voice::ToString(response.action) << "\","
         << "\"action_status\":\"" << cockpit::voice::ToString(response.action_status) << "\","
         << "\"response_text\":\"" << EscapeJson(response.response_text) << "\"}";
  return output.str();
}

}  // namespace

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
  cockpit::recording::RecordingEventPublisher recording_events(
      runtime.config().services().recording.grpc.listen_address);
  cockpit::voice::VoiceInteractionService service(
      enabled, std::move(assistant), std::move(dispatcher), std::move(output),
      [&recording_events](const cockpit::voice::VoiceResponse& response) {
        recording_events.Publish(static_cast<std::int64_t>(response.timestamp_ms),
                                 "/voice/response", VoiceResponsePayload(response));
      });
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
