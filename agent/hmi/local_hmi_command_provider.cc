#include "agent/hmi/local_hmi_command_provider.h"

#include <grpcpp/grpcpp.h>

#include <chrono>

#include "cockpit/core/logging/logger.h"

namespace cockpit {
namespace voice {

LocalHmiCommandProvider::LocalHmiCommandProvider(const std::string& address)
    : stub_(proto::hmi::HmiControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {
}

bool LocalHmiCommandProvider::SendCommand(HmiCommand command, std::string* response,
                                          std::string* error) {
  proto::hmi::ExecuteHmiCommandRequest request;
  switch (command) {
    case HmiCommand::kOpenCameraPreview:
      request.set_command(proto::hmi::HMI_COMMAND_OPEN_CAMERA_PREVIEW);
      break;
    case HmiCommand::kPlayMusic:
      request.set_command(proto::hmi::HMI_COMMAND_PLAY_MUSIC);
      break;
  }

  proto::hmi::ExecuteHmiCommandResponse command_response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  const grpc::Status status = stub_->Execute(&context, request, &command_response);
  if (!status.ok()) {
    if (error != nullptr) {
      *error =
          status.error_message().empty() ? "HMI control is unavailable" : status.error_message();
    }
    return false;
  }
  if (!command_response.executed()) {
    if (error != nullptr) {
      *error = command_response.message().empty() ? "HMI command was not executed"
                                                  : command_response.message();
    }
    return false;
  }
  if (response != nullptr) {
    *response = command_response.message();
  }
  LOG_INFO("HMI command executed command=" + std::string(ToString(command)));
  return true;
}

}  // namespace voice
}  // namespace cockpit
