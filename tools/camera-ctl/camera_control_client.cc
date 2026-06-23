#include "camera_control_client.h"

#include <chrono>

#include "common.pb.h"

namespace cockpit {
namespace camera {
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

CameraControlClient::CameraControlClient(const std::string& address)
    : stub_(proto::camera::CameraControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {
}

bool CameraControlClient::ListDevices(proto::camera::ListCameraDevicesResponse* response,
                                      std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->ListDevices(&context, request, response), error);
}

bool CameraControlClient::StartPreview(const proto::camera::StartPreviewRequest& request,
                                       proto::camera::CameraStatus* status, std::string* error) {
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->StartPreview(&context, request, status), error);
}

bool CameraControlClient::StopPreview(proto::camera::CameraStatus* status, std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->StopPreview(&context, request, status), error);
}

bool CameraControlClient::GetStatus(proto::camera::CameraStatus* status, std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetStatus(&context, request, status), error);
}

void CameraControlClient::SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

}  // namespace camera
}  // namespace cockpit
