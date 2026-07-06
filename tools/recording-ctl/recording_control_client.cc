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

bool RecordingControlClient::GetStatus(proto::recording::RecordingStatus* status,
                                       std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetStatus(&context, request, status), error);
}

void RecordingControlClient::SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

}  // namespace recording
}  // namespace cockpit
