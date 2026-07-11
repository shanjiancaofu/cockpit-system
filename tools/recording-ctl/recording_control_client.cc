#include "recording_control_client.h"

#include <chrono>

#include "common.pb.h"

namespace cockpit {
namespace recording {
namespace {

bool FinishRpc(const grpc::Status& status, std::string* error) {
  if (status.ok()) {
    return true;
  }
  if (error != nullptr) {
    *error = status.error_message();
  }
  return false;
}

}  // namespace

RecordingControlClient::RecordingControlClient(const std::string& address)
    : stub_(proto::recording::RecordingControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {
}

bool RecordingControlClient::Start(const std::string& trigger,
                                   proto::recording::RecordingStatus* status, std::string* error) {
  proto::recording::StartRecordingRequest request;
  request.set_trigger(trigger);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->Start(&context, request, status), error);
}

bool RecordingControlClient::Stop(proto::recording::RecordingStatus* status, std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->Stop(&context, request, status), error);
}

bool RecordingControlClient::AppendEvent(std::int64_t timestamp_ms, const std::string& topic,
                                         const std::string& payload_json,
                                         proto::recording::RecordingStatus* status,
                                         std::string* error) {
  proto::recording::AppendRecordingEventRequest request;
  request.set_timestamp_ms(timestamp_ms);
  request.set_topic(topic);
  request.set_payload_json(payload_json);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->AppendEvent(&context, request, status), error);
}

bool RecordingControlClient::AppendDataFile(
    const proto::recording::AppendRecordingDataFileRequest& request,
    proto::recording::RecordingStatus* status, std::string* error) {
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->AppendDataFile(&context, request, status), error);
}

bool RecordingControlClient::GetStatus(proto::recording::RecordingStatus* status,
                                       std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetStatus(&context, request, status), error);
}

bool RecordingControlClient::List(std::uint32_t limit,
                                  proto::recording::ListRecordingsResponse* response,
                                  std::string* error) {
  proto::recording::ListRecordingsRequest request;
  request.set_limit(limit);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->List(&context, request, response), error);
}

bool RecordingControlClient::GetDetail(const std::string& session_id,
                                       proto::recording::RecordingSessionDetail* response,
                                       std::string* error) {
  proto::recording::GetRecordingDetailRequest request;
  request.set_session_id(session_id);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetDetail(&context, request, response), error);
}

bool RecordingControlClient::GetTimeline(
    const proto::recording::GetRecordingTimelineRequest& request,
    proto::recording::GetRecordingTimelineResponse* response, std::string* error) {
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetTimeline(&context, request, response), error);
}

bool RecordingControlClient::Verify(const std::string& session_id,
                                    proto::recording::VerifyRecordingResponse* response,
                                    std::string* error) {
  proto::recording::VerifyRecordingRequest request;
  request.set_session_id(session_id);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->Verify(&context, request, response), error);
}

bool RecordingControlClient::Delete(const std::string& session_id, std::string* error) {
  proto::recording::DeleteRecordingRequest request;
  request.set_session_id(session_id);
  proto::common::Empty response;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->Delete(&context, request, &response), error);
}

bool RecordingControlClient::Prune(proto::recording::PruneRecordingsResponse* response,
                                   std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->Prune(&context, request, response), error);
}

void RecordingControlClient::SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
}

}  // namespace recording
}  // namespace cockpit
