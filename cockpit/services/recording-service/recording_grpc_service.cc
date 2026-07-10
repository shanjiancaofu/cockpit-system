#include "cockpit/services/recording-service/recording_grpc_service.h"

#include "cockpit/core/logging/Logger.h"
#include "cockpit/core/utils/Time.h"

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

void FillHealth(const RecordingStatus& status, proto::common::ServiceHealth* health) {
  health->set_service_name("recording-service");
  health->set_checked_at_ms(utils::NowMs());
  health->set_last_error(status.last_error);
  if (status.state == RecordingState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "recording faulted" : status.last_error);
    return;
  }
  health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
  health->set_message(status.state == RecordingState::kRecording ? "recording active"
                                                                 : "recording idle");
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

grpc::Status RecordingGrpcService::AppendEvent(
    grpc::ServerContext*, const proto::recording::AppendRecordingEventRequest* request,
    proto::recording::RecordingStatus* response) {
  RecordingEvent event;
  event.timestamp_ms = request->timestamp_ms();
  event.topic = request->topic();
  event.payload_json = request->payload_json();
  std::string error;
  if (!recording_service_.HandleEvent(event, &error)) {
    FillStatus(recording_service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(recording_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status RecordingGrpcService::AppendDataFile(
    grpc::ServerContext*, const proto::recording::AppendRecordingDataFileRequest* request,
    proto::recording::RecordingStatus* response) {
  RecordingDataFile file;
  file.timestamp_ms = request->timestamp_ms();
  file.source = request->source();
  file.kind = request->kind();
  file.path = request->path();
  file.size_bytes = request->size_bytes();
  file.checksum = request->checksum();
  file.copy_into_session = request->copy_into_session();
  std::string error;
  if (!recording_service_.HandleDataFile(file, &error)) {
    FillStatus(recording_service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
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

grpc::Status RecordingGrpcService::GetDetail(
    grpc::ServerContext*, const proto::recording::GetRecordingDetailRequest* request,
    proto::recording::RecordingSessionDetail* response) {
  if (request->session_id().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "session_id is required");
  }
  RecordingSessionDetail detail;
  std::string error;
  if (!recording_service_.GetDetail(request->session_id(), &detail, &error)) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, error);
  }
  FillDetail(detail, response);
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
  response->set_data_files_indexed(status.data_files_indexed);
  FillHealth(status, response->mutable_health());
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
  response->set_data_files_indexed(session.data_files_indexed);
}

void RecordingGrpcService::FillDetail(const RecordingSessionDetail& detail,
                                      proto::recording::RecordingSessionDetail* response) {
  FillSession(detail.info, response->mutable_info());
  response->set_project(detail.project);
  response->set_schema_version(detail.schema_version);
  response->set_vehicle_id(detail.vehicle_id);
  response->set_config_path(detail.config_path);
  response->set_config_checksum(detail.config_checksum);
  response->set_git_commit(detail.git_commit);
  response->set_git_dirty(detail.git_dirty);
  response->set_build_type(detail.build_type);
  response->set_binary_version(detail.binary_version);
  response->set_first_message_timestamp_ms(detail.first_message_timestamp_ms);
  response->set_last_message_timestamp_ms(detail.last_message_timestamp_ms);
  response->set_data_files_indexed(detail.data_files_indexed);
  for (const auto& source : detail.sources) {
    response->add_sources(source);
  }
}

}  // namespace recording
}  // namespace cockpit
