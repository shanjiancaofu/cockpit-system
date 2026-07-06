#include "cockpit/services/recording-service/recording_grpc_service.h"

#include "cockpit/core/logging/Logger.h"

namespace cockpit {
namespace recording {
namespace {

proto::recording::RecordingState ToProtoState(RecordingState state) {
  switch (state) {
    case RecordingState::kIdle:
      return proto::recording::RECORDING_STATE_IDLE;
    case RecordingState::kRecording:
      return proto::recording::RECORDING_STATE_RECORDING;
    case RecordingState::kFaulted:
      return proto::recording::RECORDING_STATE_FAULTED;
  }
  return proto::recording::RECORDING_STATE_UNSPECIFIED;
}

}  // namespace

RecordingGrpcService::RecordingGrpcService(RecordingService& recording_service)
    : recording_service_(recording_service) {
}

RecordingGrpcService::~RecordingGrpcService() {
  Shutdown();
}

bool RecordingGrpcService::Listen(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (server_ == nullptr) {
    LOG_ERROR("failed to start recording gRPC server address=" + address);
    return false;
  }
  LOG_INFO("recording gRPC server listening address=" + address);
  return true;
}

void RecordingGrpcService::Shutdown() {
  if (server_ != nullptr) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status RecordingGrpcService::Start(grpc::ServerContext*,
                                         const proto::recording::StartRecordingRequest* request,
                                         proto::recording::RecordingStatus* response) {
  std::string error;
  if (!recording_service_.Start(request->trigger(), &error)) {
    FillStatus(recording_service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(recording_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status RecordingGrpcService::Stop(grpc::ServerContext*, const proto::common::Empty*,
                                        proto::recording::RecordingStatus* response) {
  std::string error;
  if (!recording_service_.Stop(&error)) {
    FillStatus(recording_service_.status(), response);
    return grpc::Status(grpc::StatusCode::INTERNAL, error);
  }
  FillStatus(recording_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status RecordingGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                             proto::recording::RecordingStatus* response) {
  FillStatus(recording_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status RecordingGrpcService::List(grpc::ServerContext*,
                                        const proto::recording::ListRecordingsRequest* request,
                                        proto::recording::ListRecordingsResponse* response) {
  const auto all_sessions = recording_service_.List(0);
  const auto sessions = recording_service_.List(request->limit());
  for (const auto& session : sessions) {
    FillSession(session, response->add_sessions());
  }
  response->set_total_sessions(all_sessions.size());
  response->set_total_bytes(recording_service_.status().stored_bytes);
  return grpc::Status::OK;
}

grpc::Status RecordingGrpcService::Delete(grpc::ServerContext*,
                                          const proto::recording::DeleteRecordingRequest* request,
                                          proto::common::Empty*) {
  if (request->session_id().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "session_id is required");
  }
  std::string error;
  if (!recording_service_.Delete(request->session_id(), &error)) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, error);
  }
  return grpc::Status::OK;
}

grpc::Status RecordingGrpcService::Prune(grpc::ServerContext*, const proto::common::Empty*,
                                         proto::recording::PruneRecordingsResponse* response) {
  RecordingPruneResult result;
  std::string error;
  if (!recording_service_.Prune(&result, &error)) {
    return grpc::Status(grpc::StatusCode::INTERNAL, error);
  }
  const RecordingStatus status = recording_service_.status();
  response->set_sessions_deleted(result.sessions_deleted);
  response->set_bytes_deleted(result.bytes_deleted);
  response->set_sessions_remaining(status.stored_sessions);
  response->set_bytes_remaining(status.stored_bytes);
  return grpc::Status::OK;
}

void RecordingGrpcService::FillStatus(const RecordingStatus& status,
                                      proto::recording::RecordingStatus* response) {
  response->set_state(ToProtoState(status.state));
  response->set_session_id(status.session_id);
  response->set_directory(status.directory);
  response->set_trigger(status.trigger);
  response->set_messages_written(status.messages_written);
  response->set_started_at_ms(status.started_at_ms);
  response->set_stopped_at_ms(status.stopped_at_ms);
  response->set_first_message_timestamp_ms(status.first_message_timestamp_ms);
  response->set_last_message_timestamp_ms(status.last_message_timestamp_ms);
  response->set_last_error(status.last_error);
  response->set_stored_sessions(status.stored_sessions);
  response->set_stored_bytes(status.stored_bytes);
}

void RecordingGrpcService::FillSession(const RecordingSessionInfo& session,
                                       proto::recording::RecordingSessionInfo* response) {
  response->set_session_id(session.session_id);
  response->set_state(session.state);
  response->set_trigger(session.trigger);
  response->set_directory(session.directory);
  response->set_messages_written(session.messages_written);
  response->set_size_bytes(session.size_bytes);
  response->set_started_at_ms(session.started_at_ms);
  response->set_stopped_at_ms(session.stopped_at_ms);
}

}  // namespace recording
}  // namespace cockpit
