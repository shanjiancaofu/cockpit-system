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
        return proto::audio::AudioControl::NewStub(
            grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments));
      }()) {
}

bool AudioSpeechClient::Submit(std::string text) {
  proto::audio::SpeakRequest request;
  request.set_text(std::move(text));
  proto::audio::SpeakResponse response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  {
    std::lock_guard<std::mutex> lock(context_mutex_);
    if (stopping_) {
      dropped_.fetch_add(1U);
      return false;
    }
    active_context_ = &context;
  }
  const grpc::Status status = stub_->Speak(&context, request, &response);
  {
    std::lock_guard<std::mutex> lock(context_mutex_);
    active_context_ = nullptr;
  }
  if (status.ok() && response.accepted()) {
    MarkReachable();
    last_success_timestamp_ms_.store(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count()));
    queued_.fetch_add(1U);
    return true;
  }
  if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED) {
    MarkReachable();
    dropped_.fetch_add(1U);
  } else {
    available_.store(false);
    consecutive_failures_.fetch_add(1U);
    failed_.fetch_add(1U);
  }
  return false;
}

VoiceOutputMetrics AudioSpeechClient::metrics() const {
  VoiceOutputMetrics result;
  result.queued = queued_.load();
  result.failed = failed_.load();
  result.dropped = dropped_.load();
  result.reconnects = reconnects_.load();
  result.consecutive_failures = consecutive_failures_.load();
  result.last_success_timestamp_ms = last_success_timestamp_ms_.load();
  result.available = available_.load();
  return result;
}

void AudioSpeechClient::MarkReachable() {
  if (!available_.exchange(true) && connected_once_.exchange(true)) {
    reconnects_.fetch_add(1U);
  }
  consecutive_failures_.store(0);
}

void AudioSpeechClient::Stop() {
  std::lock_guard<std::mutex> lock(context_mutex_);
  stopping_ = true;
  available_.store(false);
  if (active_context_ != nullptr) {
    active_context_->TryCancel();
  }
}

}  // namespace voice
}  // namespace cockpit
