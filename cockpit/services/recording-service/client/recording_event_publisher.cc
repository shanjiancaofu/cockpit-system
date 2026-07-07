#include "cockpit/services/recording-service/client/recording_event_publisher.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <utility>

#include "cockpit/core/logging/Logger.h"
#include "recording.grpc.pb.h"

namespace cockpit {
namespace recording {
namespace {

constexpr int kAppendEventTimeoutMs = 200;

}  // namespace

RecordingEventPublisher::RecordingEventPublisher(std::string address)
    : address_(std::move(address)) {
}

bool RecordingEventPublisher::Publish(std::int64_t timestamp_ms, const std::string& topic,
                                      const std::string& payload_json) const {
  if (address_.empty() || topic.empty() || payload_json.empty()) {
    return false;
  }
  auto stub = proto::recording::RecordingControl::NewStub(
      grpc::CreateChannel(address_, grpc::InsecureChannelCredentials()));
  proto::recording::AppendRecordingEventRequest request;
  request.set_timestamp_ms(timestamp_ms);
  request.set_topic(topic);
  request.set_payload_json(payload_json);
  proto::recording::RecordingStatus response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(kAppendEventTimeoutMs));
  const grpc::Status status = stub->AppendEvent(&context, request, &response);
  if (!status.ok()) {
    LOG_WARN("append recording event failed topic=" + topic + " error=" + status.error_message());
    return false;
  }
  return true;
}

}  // namespace recording
}  // namespace cockpit
