#include "agent/grpc/voice_grpc_service.h"

#include <chrono>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

namespace cockpit {
namespace voice {
namespace {

proto::voice::InteractionState ToProtoState(InteractionState state) {
  switch (state) {
    case InteractionState::kDisabled:
      return proto::voice::INTERACTION_STATE_DISABLED;
    case InteractionState::kIdle:
      return proto::voice::INTERACTION_STATE_IDLE;
    case InteractionState::kWaking:
      return proto::voice::INTERACTION_STATE_WAKING;
    case InteractionState::kListening:
      return proto::voice::INTERACTION_STATE_LISTENING;
    case InteractionState::kRecognizing:
      return proto::voice::INTERACTION_STATE_RECOGNIZING;
    case InteractionState::kRouting:
      return proto::voice::INTERACTION_STATE_ROUTING;
    case InteractionState::kExecuting:
      return proto::voice::INTERACTION_STATE_EXECUTING;
    case InteractionState::kThinking:
      return proto::voice::INTERACTION_STATE_THINKING;
    case InteractionState::kSpeaking:
      return proto::voice::INTERACTION_STATE_SPEAKING;
    case InteractionState::kFollowUp:
      return proto::voice::INTERACTION_STATE_FOLLOW_UP;
    case InteractionState::kCancelled:
      return proto::voice::INTERACTION_STATE_CANCELLED;
    case InteractionState::kErrorRecovery:
      return proto::voice::INTERACTION_STATE_ERROR_RECOVERY;
    case InteractionState::kShuttingDown:
      return proto::voice::INTERACTION_STATE_SHUTTING_DOWN;
  }
  return proto::voice::INTERACTION_STATE_UNSPECIFIED;
}

void FillHealth(const VoiceInteractionStatus& status, proto::common::ServiceHealth* health) {
  health->set_service_name("voice-interaction-service");
  health->set_checked_at_ms(time::NowMs());
  health->set_last_error(status.last_error);
  if (status.state == InteractionState::kErrorRecovery) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "voice interaction faulted"
                                                  : status.last_error);
    return;
  }
  if (status.state == InteractionState::kDisabled) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DISABLED);
    health->set_message("voice interaction disabled");
    return;
  }
  health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
  health->set_message("voice interaction online");
}

}  // namespace

VoiceGrpcService::VoiceGrpcService(VoiceInteractionService& service) : service_(service) {
}

VoiceGrpcService::~VoiceGrpcService() {
  Shutdown();
}

bool VoiceGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("failed to start voice gRPC server address=" + address);
    return false;
  }
  LOG_INFO("voice gRPC server listening address=" + address);
  return true;
}

void VoiceGrpcService::Shutdown() {
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status VoiceGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                         proto::voice::VoiceInteractionStatus* response) {
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status VoiceGrpcService::ProcessTranscript(
    grpc::ServerContext*, const proto::voice::ProcessTranscriptRequest* request,
    proto::voice::VoiceResponseEvent* response) {
  SpeechTranscript transcript;
  transcript.text = request->text();
  const auto result = service_.HandleTranscript(transcript);
  if (!result.has_value()) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, service_.status().last_error.empty()
                                                                   ? "voice interaction is disabled"
                                                                   : service_.status().last_error);
  }
  FillResponse(*result, response);
  return grpc::Status::OK;
}

grpc::Status VoiceGrpcService::Interrupt(grpc::ServerContext*, const proto::common::Empty*,
                                         proto::voice::InterruptVoiceResponse* response) {
  const VoiceInterruptResult result = service_.Interrupt();
  response->set_active_request_interrupted(result.active_request_interrupted);
  response->set_queued_transcripts_discarded(result.queued_transcripts_discarded);
  return grpc::Status::OK;
}

grpc::Status VoiceGrpcService::SubscribeResponses(
    grpc::ServerContext* context, const proto::voice::SubscribeVoiceResponsesRequest* request,
    grpc::ServerWriter<proto::voice::VoiceResponseEvent>* writer) {
  std::uint64_t observed_id = request->after_id();
  std::uint32_t emitted = 0;
  while (!context->IsCancelled() &&
         (request->max_events() == 0 || emitted < request->max_events())) {
    VoiceResponse value;
    if (!service_.WaitForResponse(observed_id, std::chrono::milliseconds(100), &value)) {
      continue;
    }
    proto::voice::VoiceResponseEvent event;
    FillResponse(value, &event);
    if (!writer->Write(event)) {
      break;
    }
    observed_id = value.id;
    ++emitted;
  }
  return grpc::Status::OK;
}

grpc::Status VoiceGrpcService::SubscribeTranscripts(
    grpc::ServerContext* context, const proto::voice::SubscribeTranscriptsRequest* request,
    grpc::ServerWriter<proto::voice::TranscriptEvent>* writer) {
  std::uint64_t observed_id = request->after_id();
  std::uint32_t emitted = 0;
  while (!context->IsCancelled() &&
         (request->max_events() == 0 || emitted < request->max_events())) {
    SpeechTranscript value;
    if (!service_.WaitForTranscript(observed_id, std::chrono::milliseconds(100), &value)) {
      continue;
    }
    proto::voice::TranscriptEvent event;
    FillTranscript(value, &event);
    if (!writer->Write(event)) {
      break;
    }
    observed_id = value.id;
    ++emitted;
  }
  return grpc::Status::OK;
}

void VoiceGrpcService::FillResponse(const VoiceResponse& value,
                                    proto::voice::VoiceResponseEvent* response) {
  response->set_id(value.id);
  response->set_timestamp_ms(value.timestamp_ms);
  response->set_transcript_id(value.transcript_id);
  response->set_transcript_text(value.transcript_text);
  response->set_intent(ToString(value.intent));
  response->set_action(ToString(value.action));
  response->set_action_status(ToString(value.action_status));
  response->set_action_message(value.action_message);
  response->set_response_text(value.response_text);
}

void VoiceGrpcService::FillTranscript(const SpeechTranscript& value,
                                      proto::voice::TranscriptEvent* response) {
  response->set_id(value.id);
  response->set_timestamp_ms(value.timestamp_ms);
  response->set_start_sequence(value.start_sequence);
  response->set_end_sequence(value.end_sequence);
  response->set_duration_ms(value.duration_ms);
  response->set_truncated(value.truncated);
  response->set_discontinuous(value.discontinuous);
  response->set_text(value.text);
  response->set_provider(value.provider);
  response->set_confidence(value.confidence);
}

void VoiceGrpcService::FillStatus(const VoiceInteractionStatus& value,
                                  proto::voice::VoiceInteractionStatus* response) {
  response->set_state(ToProtoState(value.state));
  response->set_state_reason(value.state_reason);
  response->set_last_error(value.last_error);
  FillHealth(value, response->mutable_health());
  auto* metrics = response->mutable_metrics();
  metrics->set_transcripts_received(value.metrics.transcripts_received);
  metrics->set_transcript_events_dropped(value.metrics.transcript_events_dropped);
  metrics->set_responses_published(value.metrics.responses_published);
  metrics->set_unknown_intents(value.metrics.unknown_intents);
  metrics->set_processing_errors(value.metrics.processing_errors);
  metrics->set_upstream_reconnects(value.metrics.upstream_reconnects);
  metrics->set_actions_attempted(value.metrics.actions_attempted);
  metrics->set_actions_succeeded(value.metrics.actions_succeeded);
  metrics->set_actions_failed(value.metrics.actions_failed);
  metrics->set_requests_interrupted(value.metrics.requests_interrupted);
  metrics->set_assistant_timeouts(value.metrics.assistant_timeouts);
  metrics->set_assistant_failures(value.metrics.assistant_failures);
  metrics->set_action_timeouts(value.metrics.action_timeouts);
  metrics->set_tts_timeouts(value.metrics.output.tts_timeouts);
  metrics->set_state_transitions(value.metrics.state_transitions);
  metrics->set_rejected_state_transitions(value.metrics.rejected_state_transitions);
  metrics->set_speech_requests_accepted(value.metrics.output.queued);
  metrics->set_speech_requests_failed(value.metrics.output.failed);
  metrics->set_speech_requests_dropped(value.metrics.output.dropped);
  metrics->set_audio_playback_available(value.metrics.output.available);
  metrics->set_audio_playback_reconnects(value.metrics.output.reconnects);
  metrics->set_audio_playback_consecutive_failures(value.metrics.output.consecutive_failures);
  metrics->set_audio_playback_last_success_timestamp_ms(
      value.metrics.output.last_success_timestamp_ms);
  if (value.latest_response.has_value()) {
    FillResponse(*value.latest_response, response->mutable_latest_response());
  }
}

}  // namespace voice
}  // namespace cockpit
