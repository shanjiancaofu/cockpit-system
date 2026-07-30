#include "audio_control_client.h"

#include <chrono>

#include "common.pb.h"

namespace cockpit {
namespace audio {
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

AudioControlClient::AudioControlClient(const std::string& address)
    : stub_(proto::audio::AudioControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {
}

bool AudioControlClient::StartCapture(const std::string& input_device,
                                      proto::audio::AudioStatus* status, std::string* error) {
  proto::audio::StartCaptureRequest request;
  request.set_input_device(input_device);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->StartCapture(&context, request, status), error);
}

bool AudioControlClient::StopCapture(proto::audio::AudioStatus* status, std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->StopCapture(&context, request, status), error);
}

bool AudioControlClient::GetStatus(proto::audio::AudioStatus* status, std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetStatus(&context, request, status), error);
}

void AudioControlClient::SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

}  // namespace audio
}  // namespace cockpit
