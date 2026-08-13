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

bool LocalHmiCommandProvider::SendCommand(HmiCommand command,
                                          std::chrono::steady_clock::time_point deadline,
                                          std::string* response, std::string* error) {
  const std::uint64_t generation = cancellation_generation_.load();
  const auto remaining = deadline - std::chrono::steady_clock::now();
  if (remaining <= std::chrono::steady_clock::duration::zero()) {
    if (error != nullptr) {
      *error = "HMI command deadline exceeded";
    }
    return false;
  }
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
  context.set_deadline(std::chrono::system_clock::now() + remaining);
  {
    std::lock_guard<std::mutex> lock(cancellation_mutex_);
    if (cancellation_generation_.load() != generation) {
      if (error != nullptr) {
        *error = "HMI command cancelled";
      }
      return false;
    }
    active_context_ = &context;
  }
  const grpc::Status status = stub_->Execute(&context, request, &command_response);
  {
    std::lock_guard<std::mutex> lock(cancellation_mutex_);
    if (active_context_ == &context) {
      active_context_ = nullptr;
    }
  }
  if (cancellation_generation_.load() != generation) {
    if (error != nullptr) {
      *error = "HMI command cancelled";
    }
    return false;
  }
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

void LocalHmiCommandProvider::Cancel() {
  cancellation_generation_.fetch_add(1U);
  std::lock_guard<std::mutex> lock(cancellation_mutex_);
  if (active_context_ != nullptr) {
    active_context_->TryCancel();
  }
}

}  // namespace voice
}  // namespace cockpit
