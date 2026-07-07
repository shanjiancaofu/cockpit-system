#include "voice_grpc_service.h"

#include <chrono>

#include "cockpit/core/logging/Logger.h"
#include "cockpit/core/utils/Time.h"

namespace cockpit {
namespace voice {
namespace {

proto::voice::InteractionState ToProtoState(InteractionState state) {
  switch (state) {
    case InteractionState::kDisabled:
      return proto::voice::INTERACTION_STATE_DISABLED;
    case InteractionState::kListening:
      return proto::voice::INTERACTION_STATE_LISTENING;
    case InteractionState::kProcessing:
      return proto::voice::INTERACTION_STATE_PROCESSING;
    case InteractionState::kFaulted:
      return proto::voice::INTERACTION_STATE_FAULTED;
  }
  return proto::voice::INTERACTION_STATE_UNSPECIFIED;
}

void FillHealth(const VoiceInteractionStatus& status, proto::common::ServiceHealth* health) {
  health->set_service_name("voice-interaction-service");
  health->set_checked_at_ms(utils::NowMs());
  health->set_last_error(status.last_error);
  if (status.state == InteractionState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "voice interaction faulted"
                                                  : status.last_error);
    return;
  }
  if (status.state == InteractionState::kDisabled) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
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

void VoiceGrpcService::FillStatus(const VoiceInteractionStatus& value,
                                  proto::voice::VoiceInteractionStatus* response) {
  response->set_state(ToProtoState(value.state));
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
  metrics->set_speech_requests_accepted(value.metrics.output.queued);
  metrics->set_speech_requests_failed(value.metrics.output.failed);
  metrics->set_speech_requests_dropped(value.metrics.output.dropped);
  metrics->set_speech_output_available(value.metrics.output.available);
  metrics->set_speech_output_reconnects(value.metrics.output.reconnects);
  metrics->set_speech_output_consecutive_failures(value.metrics.output.consecutive_failures);
  metrics->set_speech_output_last_success_timestamp_ms(
      value.metrics.output.last_success_timestamp_ms);
  if (value.latest_response.has_value()) {
    FillResponse(*value.latest_response, response->mutable_latest_response());
  }
}

}  // namespace voice
}  // namespace cockpit
