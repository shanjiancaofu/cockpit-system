#include "agent/runtime/agent_runtime.h"

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

#include "agent/audio/audio_playback_client.h"
#include "agent/audio/audio_playback_transport.h"
#include "agent/audio/audio_stream_client.h"
#include "agent/grpc/voice_grpc_service.h"
#include "agent/hmi/local_hmi_command_provider.h"
#include "agent/interaction/voice_interaction_service.h"
#include "agent/llm/llama_server_local_llm_client.h"
#include "agent/llm/llama_server_process.h"
#include "agent/llm/mock_local_llm_client.h"
#include "agent/runtime/voice_input_gate.h"
#include "agent/speech/asr/mock_speech_recognizer.h"
#include "agent/speech/kws/fixed_pcm_wake_prompt_player.h"
#include "agent/speech/kws/mock_wake_word_detector.h"
#include "agent/speech/pipeline/speech_pipeline.h"
#include "agent/speech/tts/mock_speech_synthesizer.h"
#include "agent/speech/vad/mock_voice_activity_detector.h"
#include "agent/vehicle/gateway_vehicle_status_client.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/core/json/json.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/modules/recording/client/recording_event_publisher.h"
#include "cockpit/modules/voice/actions/cockpit_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"
#include "cockpit/modules/voice/responses/async_voice_response_sink.h"

#if defined(COCKPIT_ENABLE_SHERPA_AGENT)
#include "agent/speech/providers/sherpa/sherpa_kokoro_speech_synthesizer.h"
#include "agent/speech/providers/sherpa/sherpa_sensevoice_recognizer.h"
#include "agent/speech/providers/sherpa/sherpa_voice_activity_detector.h"
#include "agent/speech/providers/sherpa/sherpa_wake_word_detector.h"
#endif

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

std::unique_ptr<WakeWordDetector> CreateWakeWordDetector(const config::KwsConfig& config,
                                                         std::string* error) {
  if (!config.enabled) {
    return nullptr;
  }
  if (config.provider == "mock") {
    return std::make_unique<MockWakeWordDetector>(config.wake_word);
  }
  if (config.provider == "sherpa") {
#if defined(COCKPIT_ENABLE_SHERPA_AGENT)
    return CreateSherpaWakeWordDetector(config);
#else
    if (error != nullptr) {
      *error = "Sherpa KWS provider requested, but COCKPIT_ENABLE_SHERPA_AGENT is OFF";
    }
    return nullptr;
#endif
  }
  if (error != nullptr) {
    *error = "unsupported KWS provider: " + config.provider;
  }
  return nullptr;
}

std::unique_ptr<audio::VoiceActivityDetector> CreateVoiceActivityDetector(
    const config::VadConfig& config, std::string* error) {
  if (config.provider == "mock" || config.provider == "disabled") {
    return std::make_unique<MockVoiceActivityDetector>();
  }
  if (config.provider == "sherpa") {
#if defined(COCKPIT_ENABLE_SHERPA_AGENT)
    return CreateSherpaVoiceActivityDetector();
#else
    if (error != nullptr) {
      *error = "Sherpa VAD provider requested, but COCKPIT_ENABLE_SHERPA_AGENT is OFF";
    }
    return nullptr;
#endif
  }
  if (error != nullptr) {
    *error = "unsupported VAD provider: " + config.provider;
  }
  return nullptr;
}

std::unique_ptr<voice::SpeechRecognizer> CreateSpeechRecognizer(const config::AsrConfig& config,
                                                                std::string* error) {
  if (config.provider == "mock") {
    return std::make_unique<voice::MockSpeechRecognizer>();
  }
  if (config.provider == "sherpa-sensevoice") {
#if defined(COCKPIT_ENABLE_SHERPA_AGENT)
    return cockpit::voice::CreateSherpaSenseVoiceRecognizer();
#else
    if (error != nullptr) {
      *error = "Sherpa ASR provider requested, but COCKPIT_ENABLE_SHERPA_AGENT is OFF";
    }
    return nullptr;
#endif
  }
  if (error != nullptr) {
    *error = "unsupported ASR provider: " + config.provider;
  }
  return nullptr;
}

std::unique_ptr<voice::SpeechSynthesizer> CreateSpeechSynthesizer(const config::TtsConfig& config,
                                                                  std::string* error) {
  if (config.provider == "mock") {
    return std::make_unique<voice::MockSpeechSynthesizer>();
  }
  if (config.provider == "sherpa-kokoro") {
#if defined(COCKPIT_ENABLE_SHERPA_AGENT)
    try {
      return voice::CreateSherpaKokoroSpeechSynthesizer(config);
    } catch (const std::exception& exception) {
      if (error != nullptr) {
        *error = exception.what();
      }
      return nullptr;
    }
#else
    if (error != nullptr) {
      *error = "Sherpa Kokoro TTS requested, but COCKPIT_ENABLE_SHERPA_AGENT is OFF";
    }
    return nullptr;
#endif
  }
  if (error != nullptr) {
    *error = "unsupported TTS provider: " + config.provider;
  }
  return nullptr;
}

std::unique_ptr<voice::LocalLlmClient> CreateLocalLlmClient(const config::LocalLlmConfig& config,
                                                            std::string* error) {
  if (!config.enabled || config.provider == "disabled") {
    return nullptr;
  }
  if (config.provider == "mock") {
    return std::make_unique<voice::MockLocalLlmClient>();
  }
  if (config.provider == "llama-server") {
    voice::LocalLlmConfig client_config;
    client_config.provider = config.provider;
    client_config.host = config.host;
    client_config.port = static_cast<std::uint16_t>(config.port);
    client_config.path = config.path;
    client_config.model = config.model;
    client_config.system_prompt = config.system_prompt;
    client_config.max_tokens = static_cast<std::size_t>(config.max_tokens);
    client_config.temperature = config.temperature;
    client_config.first_token_timeout = std::chrono::milliseconds(config.first_token_timeout_ms);
    return std::make_unique<voice::LlamaServerLocalLlmClient>(std::move(client_config));
  }
  if (error != nullptr) {
    *error = "unsupported local LLM provider: " + config.provider;
  }
  return nullptr;
}

std::unique_ptr<voice::LlamaServerProcess> CreateLlamaServerProcess(
    const config::LocalLlmConfig& config) {
  if (!config.enabled || config.provider != "llama-server" || !config.manage_process) {
    return nullptr;
  }
  voice::LlamaServerProcessConfig process_config;
  process_config.executable = config.executable;
  process_config.model_path = config.model_path;
  process_config.model_alias = config.model;
  process_config.host = config.host;
  process_config.port = static_cast<std::uint16_t>(config.port);
  process_config.context_size = config.context_size;
  process_config.gpu_layers = config.gpu_layers;
  process_config.startup_timeout = std::chrono::milliseconds(config.startup_timeout_ms);
  return std::make_unique<voice::LlamaServerProcess>(std::move(process_config));
}

}  // namespace

class AgentRuntime::Impl {
 public:
  std::unique_ptr<recording::RecordingEventPublisher> recording_events;
  std::unique_ptr<voice::LlamaServerProcess> llm_server;
  std::unique_ptr<voice::VoiceInteractionService> service;
  std::unique_ptr<voice::VoiceGrpcService> grpc;
  std::unique_ptr<SpeechPipeline> speech_pipeline;
  std::unique_ptr<VoiceInputGate> input_gate;
  std::unique_ptr<AudioStreamClient> audio_stream;
  std::string audio_stream_path;
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
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    const bool enabled = config.features().voice.enabled || force_enable;

    std::unique_ptr<voice::VoiceAssistant> assistant;
    std::unique_ptr<voice::ActionDispatcher> dispatcher;
    std::unique_ptr<voice::VoiceResponseSink> output;
    std::unique_ptr<voice::LocalLlmClient> llm_client;
    std::unique_ptr<voice::LlamaServerProcess> llm_server;
    if (enabled) {
      const std::string hmi_address =
          "unix:" + std::filesystem::absolute(std::filesystem::path(config.paths().run_dir) /
                                              "hmi-control.sock")
                        .string();
      assistant = std::make_unique<voice::MockVoiceAssistant>();
      dispatcher = std::make_unique<voice::CockpitActionDispatcher>(
          std::make_unique<voice::GatewayVehicleStatusClient>(interaction_config.gateway_address),
          std::make_unique<voice::LocalHmiCommandProvider>(hmi_address));
      std::string tts_error;
      auto synthesizer = CreateSpeechSynthesizer(config.features().voice.tts, &tts_error);
      if (synthesizer == nullptr) {
        LOG_ERROR("failed to configure TTS: " + tts_error);
        return false;
      }
      output = std::make_unique<voice::AsyncVoiceResponseSink>(
          std::make_unique<voice::AudioPlaybackClient>(
              interaction_config.audio_address, std::move(synthesizer),
              std::chrono::milliseconds(config.features().ai.tts_synthesis_timeout_ms),
              std::make_unique<voice::MockSpeechSynthesizer>()));
      std::string llm_error;
      llm_server = CreateLlamaServerProcess(config.features().ai.local_llm);
      if (llm_server != nullptr && !llm_server->Start(&llm_error)) {
        LOG_ERROR("failed to start local LLM server: " + llm_error);
        return false;
      }
      llm_client = CreateLocalLlmClient(config.features().ai.local_llm, &llm_error);
      if (config.features().ai.local_llm.enabled && llm_client == nullptr) {
        LOG_ERROR("failed to configure local LLM: " + llm_error);
        return false;
      }
    }

    impl_ = std::make_unique<Impl>();
    impl_->llm_server = std::move(llm_server);
    if (enabled) {
      std::string vad_error;
      auto vad = CreateVoiceActivityDetector(config.features().voice.vad, &vad_error);
      if (vad == nullptr) {
        LOG_ERROR("failed to configure VAD: " + vad_error);
        impl_.reset();
        return false;
      }
      std::string asr_error;
      auto asr = CreateSpeechRecognizer(config.features().voice.asr, &asr_error);
      if (asr == nullptr) {
        LOG_ERROR("failed to configure ASR: " + asr_error);
        impl_.reset();
        return false;
      }
      impl_->speech_pipeline = std::make_unique<SpeechPipeline>(
          config.hardware().audio, config.features().voice.speech_segment, std::move(vad),
          std::move(asr), std::chrono::milliseconds(config.features().ai.asr_timeout_ms));
      impl_->audio_stream = std::make_unique<AudioStreamClient>();
      impl_->audio_stream_path =
          std::filesystem::absolute(std::filesystem::path(config.paths().run_dir) /
                                    "audio-capture.sock")
              .string();
    }
    impl_->recording_events = std::make_unique<recording::RecordingEventPublisher>(
        config.services().recording.grpc.listen_address);
    recording::RecordingEventPublisher* recording_events = impl_->recording_events.get();
    impl_->service = std::make_unique<voice::VoiceInteractionService>(
        enabled, std::move(assistant), std::move(dispatcher), std::move(output),
        [recording_events](const voice::VoiceResponse& response) {
          recording_events->Publish(static_cast<std::int64_t>(response.timestamp_ms),
                                    "/voice/response", VoiceResponsePayload(response));
        },
        std::chrono::milliseconds(config.features().ai.assistant_timeout_ms),
        std::chrono::milliseconds(config.features().ai.command_execution_timeout_ms),
        std::chrono::milliseconds(config.features().ai.follow_up_window_ms), std::move(llm_client),
        std::chrono::milliseconds(config.features().ai.local_llm.response_timeout_ms));
    impl_->grpc = std::make_unique<voice::VoiceGrpcService>(*impl_->service);
    if (!impl_->grpc->Start(interaction_config.grpc.listen_address)) {
      impl_.reset();
      return false;
    }

    impl_->running.store(true);
    if (enabled) {
      impl_->service->Start();
      std::string kws_error;
      auto detector = CreateWakeWordDetector(config.features().voice.kws, &kws_error);
      if (config.features().voice.kws.enabled && detector == nullptr) {
        LOG_ERROR("failed to configure KWS: " + kws_error);
        impl_.reset();
        return false;
      }
      std::unique_ptr<WakePromptPlayer> wake_prompt;
      if (config.features().voice.kws.enabled) {
        wake_prompt = std::make_unique<FixedPcmWakePromptPlayer>(
            voice::CreateGrpcAudioPlaybackTransport(interaction_config.audio_address));
      }
      impl_->input_gate = std::make_unique<VoiceInputGate>(
          config.features().voice.kws, impl_->service.get(), impl_->speech_pipeline.get(),
          std::move(detector), std::move(wake_prompt));
      std::string pipeline_error;
      if (!impl_->speech_pipeline->Start(
              [this](const voice::SpeechTranscript& transcript) {
                impl_->service->SubmitTranscript(transcript);
              },
              &pipeline_error)) {
        LOG_ERROR("failed to start speech pipeline: " + pipeline_error);
        impl_.reset();
        return false;
      }
      impl_->worker = std::thread([this] {
        while (!impl_->stopping.load()) {
          if (!impl_->audio_stream->connected()) {
            std::string error;
            if (!impl_->audio_stream->Connect(impl_->audio_stream_path, &error)) {
              impl_->service->SetLastError(error);
              impl_->service->RecordUpstreamReconnect();
              std::this_thread::sleep_for(std::chrono::milliseconds(200));
              continue;
            }
          }
          const AudioStreamReceiveResult result = impl_->audio_stream->ReceiveFrame(100);
          if (result.status == AudioStreamReceiveStatus::kFrame) {
            impl_->input_gate->ProcessFrame(*result.frame);
          } else if (result.status == AudioStreamReceiveStatus::kDisconnected ||
                     result.status == AudioStreamReceiveStatus::kError) {
            impl_->service->SetLastError(result.error);
            impl_->audio_stream->Close();
            impl_->service->RecordUpstreamReconnect();
          }
        }
        impl_->result.store(0);
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
  if (impl_->input_gate != nullptr) {
    impl_->input_gate->Stop();
  }
  if (impl_->audio_stream != nullptr) {
    impl_->audio_stream->Close();
  }
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  if (impl_->speech_pipeline != nullptr) {
    impl_->speech_pipeline->Stop();
  }
  impl_->service->Stop();
  impl_->grpc->Shutdown();
  if (impl_->llm_server != nullptr) {
    impl_->llm_server->Stop();
  }
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
