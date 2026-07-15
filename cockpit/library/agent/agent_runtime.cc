#include "cockpit/library/agent/agent_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/json/json.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/agent/audio/audio_speech_client.h"
#include "cockpit/library/agent/audio/audio_transcript_client.h"
#include "cockpit/library/agent/grpc/voice_grpc_service.h"
#include "cockpit/library/agent/hmi/local_hmi_command_provider.h"
#include "cockpit/library/agent/interaction/voice_interaction_service.h"
#include "cockpit/library/agent/vehicle/gateway_vehicle_status_client.h"
#include "cockpit/modules/recording/client/recording_event_publisher.h"
#include "cockpit/modules/voice/actions/cockpit_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"
#include "cockpit/modules/voice/responses/async_voice_response_sink.h"

namespace cockpit {
namespace agent {
namespace {

std::string VoiceResponsePayload(const voice::VoiceResponse& response) {
  std::ostringstream output;
  output << "{"
         << "\"id\":" << response.id << ',' << "\"transcript_id\":" << response.transcript_id << ','
         << "\"transcript_text\":\"" << json::EscapeString(response.transcript_text) << "\","
         << "\"intent\":\"" << voice::ToString(response.intent) << "\","
         << "\"action\":\"" << voice::ToString(response.action) << "\","
         << "\"action_status\":\"" << voice::ToString(response.action_status) << "\","
         << "\"response_text\":\"" << json::EscapeString(response.response_text) << "\"}";
  return output.str();
}

}  // namespace

class AgentRuntime::Impl {
 public:
  std::unique_ptr<recording::RecordingEventPublisher> recording_events;
  std::unique_ptr<voice::VoiceInteractionService> service;
  std::unique_ptr<voice::VoiceGrpcService> grpc;
  std::unique_ptr<voice::AudioTranscriptClient> transcript_client;
  std::thread worker;
  std::atomic_bool stopping{false};
  std::atomic_bool running{false};
  std::atomic_int result{0};
};

AgentRuntime::AgentRuntime() = default;

AgentRuntime::~AgentRuntime() {
  Stop();
}

bool AgentRuntime::Start(const std::string& config_path, bool force_enable) {
  if (impl_ != nullptr) {
    return false;
  }

  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    const auto& interaction_config = config.services().voice_interaction;
    logging::InitLogger("agent", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().max_bytes,
                        config.logging().mirror_stderr);
    const bool enabled = config.features().voice.enabled || force_enable;

    std::unique_ptr<voice::VoiceAssistant> assistant;
    std::unique_ptr<voice::ActionDispatcher> dispatcher;
    std::unique_ptr<voice::VoiceResponseSink> output;
    if (enabled) {
      const std::string hmi_address =
          "unix:" + std::filesystem::absolute(std::filesystem::path(config.paths().run_dir) /
                                              "hmi-control.sock")
                        .string();
      assistant = std::make_unique<voice::MockVoiceAssistant>();
      dispatcher = std::make_unique<voice::CockpitActionDispatcher>(
          std::make_unique<voice::GatewayVehicleStatusClient>(interaction_config.gateway_address),
          std::make_unique<voice::LocalHmiCommandProvider>(hmi_address));
      output = std::make_unique<voice::AsyncVoiceResponseSink>(
          std::make_unique<voice::AudioSpeechClient>(interaction_config.audio_address));
    }

    impl_ = std::make_unique<Impl>();
    impl_->recording_events = std::make_unique<recording::RecordingEventPublisher>(
        config.services().recording.grpc.listen_address);
    recording::RecordingEventPublisher* recording_events = impl_->recording_events.get();
    impl_->service = std::make_unique<voice::VoiceInteractionService>(
        enabled, std::move(assistant), std::move(dispatcher), std::move(output),
        [recording_events](const voice::VoiceResponse& response) {
          recording_events->Publish(static_cast<std::int64_t>(response.timestamp_ms),
                                    "/voice/response", VoiceResponsePayload(response));
        },
        std::chrono::milliseconds(config.features().ai.request_timeout_ms));
    impl_->grpc = std::make_unique<voice::VoiceGrpcService>(*impl_->service);
    if (!impl_->grpc->Start(interaction_config.grpc.listen_address)) {
      impl_.reset();
      return false;
    }

    impl_->running.store(true);
    if (enabled) {
      impl_->service->Start();
      impl_->transcript_client = std::make_unique<voice::AudioTranscriptClient>(
          interaction_config.audio_address, interaction_config.stream_timeout_ms,
          interaction_config.retry_delay_ms);
      impl_->worker = std::thread([this] {
        int result = impl_->transcript_client->Stream(
            [this](const voice::SpeechTranscript& transcript) {
              impl_->service->SubmitTranscript(transcript);
            },
            [this] {
              return !impl_->stopping.load();
            },
            [this] {
              impl_->service->RecordUpstreamReconnect();
            },
            [this](const std::string& error) {
              impl_->service->SetLastError(error);
            });
        if (!impl_->stopping.load() && result == 0) {
          result = 1;
        }
        impl_->result.store(result);
        impl_->running.store(false);
      });
    }
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure agent: " + std::string(error.what()));
    impl_.reset();
    return false;
  }
}

void AgentRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->stopping.store(true);
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  impl_->service->Stop();
  impl_->grpc->Shutdown();
  impl_.reset();
}

int AgentRuntime::Poll() const {
  if (impl_ == nullptr) {
    return 1;
  }
  if (impl_->running.load()) {
    return 0;
  }
  const int result = impl_->result.load();
  return result == 0 ? 1 : result;
}

}  // namespace agent
}  // namespace cockpit
