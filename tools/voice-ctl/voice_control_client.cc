#include "voice_control_client.h"

#include <chrono>

#include "common.pb.h"

namespace cockpit {
namespace voice {
namespace {

constexpr auto kControlDeadline = std::chrono::seconds(7);

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

VoiceControlClient::VoiceControlClient(const std::string& address)
    : stub_(proto::voice::VoiceInteractionControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {
}

bool VoiceControlClient::GetStatus(proto::voice::VoiceInteractionStatus* status,
                                   std::string* error) {
  proto::common::Empty request;
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->GetStatus(&context, request, status), error);
}

bool VoiceControlClient::ProcessTranscript(const std::string& text,
                                           proto::voice::VoiceResponseEvent* response,
                                           std::string* error) {
  proto::voice::ProcessTranscriptRequest request;
  request.set_text(text);
  grpc::ClientContext context;
  SetDeadline(&context);
  return FinishRpc(stub_->ProcessTranscript(&context, request, response), error);
}

bool VoiceControlClient::SubscribeResponses(std::uint64_t after_id, std::uint32_t count,
                                            int timeout_ms, const ResponseHandler& handler,
                                            std::string* error) {
  proto::voice::SubscribeVoiceResponsesRequest request;
  request.set_client_id("voice-ctl");
  request.set_after_id(after_id);
  request.set_max_events(count);
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms));
  auto reader = stub_->SubscribeResponses(&context, request);
  proto::voice::VoiceResponseEvent response;
  while (reader->Read(&response)) {
    handler(response);
  }
  return FinishRpc(reader->Finish(), error);
}

void VoiceControlClient::SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + kControlDeadline);
}

}  // namespace voice
}  // namespace cockpit
