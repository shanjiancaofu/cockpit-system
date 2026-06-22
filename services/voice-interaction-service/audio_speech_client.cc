#include "audio_speech_client.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <utility>

namespace cockpit {
namespace voice {

AudioSpeechClient::AudioSpeechClient(const std::string& address)
    : stub_([&address] {
        grpc::ChannelArguments arguments;
        arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        return proto::audio::AudioControl::NewStub(grpc::CreateCustomChannel(
            address, grpc::InsecureChannelCredentials(), arguments));
      }()) {}

bool AudioSpeechClient::Submit(std::string text) {
  proto::audio::SpeakRequest request;
  request.set_text(std::move(text));
  proto::audio::SpeakResponse response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(2));
  const grpc::Status status = stub_->Speak(&context, request, &response);
  if (status.ok() && response.accepted()) {
    queued_.fetch_add(1U);
    return true;
  }
  if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED) {
    dropped_.fetch_add(1U);
  } else {
    failed_.fetch_add(1U);
  }
  return false;
}

VoiceOutputMetrics AudioSpeechClient::metrics() const {
  VoiceOutputMetrics result;
  result.queued = queued_.load();
  result.failed = failed_.load();
  result.dropped = dropped_.load();
  return result;
}

}  // namespace voice
}  // namespace cockpit
