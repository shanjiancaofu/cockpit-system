#include "voice_grpc_service.h"

#include "core/logging/Logger.h"

#include <chrono>

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

}  // namespace

VoiceGrpcService::VoiceGrpcService(VoiceInteractionService& service)
    : service_(service) {}

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

grpc::Status VoiceGrpcService::GetStatus(
    grpc::ServerContext*, const proto::common::Empty*,
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
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        service_.status().last_error.empty()
                            ? "voice interaction is disabled"
                            : service_.status().last_error);
  }
  FillResponse(*result, response);
  return grpc::Status::OK;
}

grpc::Status VoiceGrpcService::SubscribeResponses(
    grpc::ServerContext* context,
    const proto::voice::SubscribeVoiceResponsesRequest* request,
    grpc::ServerWriter<proto::voice::VoiceResponseEvent>* writer) {
  std::uint64_t observed_id = request->after_id();
  std::uint32_t emitted = 0;
  while (!context->IsCancelled() &&
         (request->max_events() == 0 || emitted < request->max_events())) {
    VoiceResponse value;
    if (!service_.WaitForResponse(observed_id, std::chrono::milliseconds(100),
                                  &value)) {
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

void VoiceGrpcService::FillResponse(
    const VoiceResponse& value, proto::voice::VoiceResponseEvent* response) {
  response->set_id(value.id);
  response->set_timestamp_ms(value.timestamp_ms);
  response->set_transcript_id(value.transcript_id);
  response->set_transcript_text(value.transcript_text);
  response->set_intent(ToString(value.intent));
  response->set_action(ToString(value.action));
  response->set_response_text(value.response_text);
}

void VoiceGrpcService::FillStatus(
    const VoiceInteractionStatus& value,
    proto::voice::VoiceInteractionStatus* response) {
  response->set_state(ToProtoState(value.state));
  response->set_last_error(value.last_error);
  auto* metrics = response->mutable_metrics();
  metrics->set_transcripts_received(value.metrics.transcripts_received);
  metrics->set_responses_published(value.metrics.responses_published);
  metrics->set_unknown_intents(value.metrics.unknown_intents);
  metrics->set_processing_errors(value.metrics.processing_errors);
  metrics->set_upstream_reconnects(value.metrics.upstream_reconnects);
  if (value.latest_response.has_value()) {
    FillResponse(*value.latest_response, response->mutable_latest_response());
  }
}

}  // namespace voice
}  // namespace cockpit
