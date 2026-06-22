#include "audio_transcript_client.h"

#include "audio.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace cockpit {
namespace voice {

AudioTranscriptClient::AudioTranscriptClient(std::string address,
                                             int stream_timeout_ms,
                                             int retry_delay_ms)
    : address_(std::move(address)),
      stream_timeout_ms_(stream_timeout_ms),
      retry_delay_ms_(retry_delay_ms) {}

int AudioTranscriptClient::Stream(
    const TranscriptHandler& transcript_handler,
    const ContinueHandler& should_continue,
    const ReconnectHandler& reconnect_handler,
    const ErrorHandler& error_handler) {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(
      address_, grpc::InsecureChannelCredentials(), arguments);
  auto stub = proto::audio::AudioControl::NewStub(channel);
  std::uint64_t observed_id = 0;

  while (should_continue()) {
    grpc::ClientContext context;
    context.set_wait_for_ready(true);
    context.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(stream_timeout_ms_));
    proto::audio::SubscribeTranscriptsRequest request;
    request.set_client_id("voice-interaction-service");
    request.set_after_id(observed_id);
    auto reader = stub->SubscribeTranscripts(&context, request);

    std::atomic_bool stream_finished{false};
    std::thread stop_watcher([&context, &should_continue, &stream_finished] {
      while (!stream_finished.load() && should_continue()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (!should_continue()) {
        context.TryCancel();
      }
    });

    proto::audio::TranscriptEvent event;
    while (reader->Read(&event)) {
      SpeechTranscript transcript;
      transcript.id = event.id();
      transcript.timestamp_ms = event.timestamp_ms();
      transcript.start_sequence = event.start_sequence();
      transcript.end_sequence = event.end_sequence();
      transcript.duration_ms = event.duration_ms();
      transcript.truncated = event.truncated();
      transcript.discontinuous = event.discontinuous();
      transcript.text = event.text();
      transcript.provider = event.provider();
      transcript.confidence = event.confidence();
      transcript_handler(transcript);
      observed_id = event.id();
    }
    const grpc::Status status = reader->Finish();
    stream_finished.store(true);
    stop_watcher.join();
    if (!should_continue()) {
      return 0;
    }
    reconnect_handler();
    if (!status.ok() && status.error_code() != grpc::StatusCode::DEADLINE_EXCEEDED &&
        status.error_code() != grpc::StatusCode::UNAVAILABLE &&
        status.error_code() != grpc::StatusCode::CANCELLED) {
      error_handler(status.error_message());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms_));
  }
  return 0;
}

}  // namespace voice
}  // namespace cockpit
