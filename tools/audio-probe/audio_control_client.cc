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

bool AudioControlClient::Speak(const std::string& text, std::string* error) {
  proto::audio::SpeakRequest request;
  request.set_text(text);
  proto::audio::SpeakResponse response;
  grpc::ClientContext context;
  SetDeadline(&context);
  const grpc::Status status = stub_->Speak(&context, request, &response);
  return FinishRpc(status, error) && response.accepted();
}

bool AudioControlClient::SubscribeTranscripts(std::uint32_t count, int timeout_ms,
                                              const TranscriptHandler& handler,
                                              std::string* error) {
  proto::audio::SubscribeTranscriptsRequest request;
  request.set_client_id("audio-probe");
  request.set_max_events(count);
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms));
  auto reader = stub_->SubscribeTranscripts(&context, request);
  proto::audio::TranscriptEvent event;
  while (reader->Read(&event)) {
    handler(event);
  }
  return FinishRpc(reader->Finish(), error);
}

void AudioControlClient::SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

}  // namespace audio
}  // namespace cockpit
