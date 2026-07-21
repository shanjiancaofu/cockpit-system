#include "cockpit/modules/recording/client/recording_event_publisher.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <utility>

#include "cockpit/core/logging/logger.h"
#include "recording.grpc.pb.h"

namespace cockpit {
namespace recording {
namespace {

constexpr int kAppendEventTimeoutMs = 200;
constexpr auto kUnavailableRetryDelay = std::chrono::seconds(1);

std::shared_ptr<grpc::Channel> CreateRecordingChannel(const std::string& address) {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 200);
  arguments.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 200);
  arguments.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000);
  return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments);
}

}  // namespace

RecordingEventPublisher::RecordingEventPublisher(const std::string& address)
    : stub_(proto::recording::RecordingControl::NewStub(CreateRecordingChannel(address))),
      worker_(&RecordingEventPublisher::Run, this) {
}

RecordingEventPublisher::~RecordingEventPublisher() {
  Stop();
}

bool RecordingEventPublisher::Publish(std::int64_t timestamp_ms, const std::string& topic,
                                      const std::string& payload_json) {
  if (timestamp_ms <= 0 || topic.empty() || payload_json.empty()) {
    return false;
  }
  PendingRequest request;
  request.kind = PendingRequest::Kind::kEvent;
  request.event.set_timestamp_ms(timestamp_ms);
  request.event.set_topic(topic);
  request.event.set_payload_json(payload_json);
  return Enqueue(std::move(request));
}

bool RecordingEventPublisher::PublishDataFile(
    proto::recording::AppendRecordingDataFileRequest request) {
  if (request.timestamp_ms() <= 0 || request.source().empty() || request.kind().empty() ||
      request.path().empty()) {
    return false;
  }
  PendingRequest pending;
  pending.kind = PendingRequest::Kind::kDataFile;
  pending.data_file = std::move(request);
  return Enqueue(std::move(pending));
}

void RecordingEventPublisher::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    stopping_ = true;
    queue_.clear();
    if (active_context_ != nullptr) {
      active_context_->TryCancel();
    }
  }
  changed_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool RecordingEventPublisher::Enqueue(PendingRequest request) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || queue_.size() >= kQueueCapacity) {
      return false;
    }
    queue_.push_back(std::move(request));
  }
  changed_.notify_one();
  return true;
}

void RecordingEventPublisher::Run() {
  bool transport_unavailable = false;
  std::size_t dropped_while_unavailable = 0;
  auto retry_after = std::chrono::steady_clock::time_point::min();
  while (true) {
    PendingRequest request;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this] {
        return stopping_ || !queue_.empty();
      });
      if (stopping_) {
        return;
      }
      request = std::move(queue_.front());
      queue_.pop_front();
    }

    if (std::chrono::steady_clock::now() < retry_after) {
      ++dropped_while_unavailable;
      continue;
    }

    proto::recording::RecordingStatus response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(kAppendEventTimeoutMs));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      active_context_ = &context;
    }

    grpc::Status status;
    if (request.kind == PendingRequest::Kind::kEvent) {
      status = stub_->AppendEvent(&context, request.event, &response);
    } else {
      status = stub_->AppendDataFile(&context, request.data_file, &response);
    }

    bool stopping = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_context_ = nullptr;
      stopping = stopping_;
    }
    const bool recording_inactive = status.error_code() == grpc::StatusCode::FAILED_PRECONDITION &&
                                    status.error_message() == "recording session is not active";
    const bool transport_failure = status.error_code() == grpc::StatusCode::UNAVAILABLE ||
                                   status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED;
    if (transport_failure) {
      ++dropped_while_unavailable;
      retry_after = std::chrono::steady_clock::now() + kUnavailableRetryDelay;
      if (!transport_unavailable) {
        LOG_WARN("recording service unavailable; best-effort events will be dropped until retry");
        transport_unavailable = true;
      }
      continue;
    }
    if (transport_unavailable) {
      LOG_INFO("recording event publisher recovered dropped_requests=" +
               std::to_string(dropped_while_unavailable));
      transport_unavailable = false;
      dropped_while_unavailable = 0;
      retry_after = std::chrono::steady_clock::time_point::min();
    }
    if (!status.ok() && !recording_inactive &&
        !(stopping && status.error_code() == grpc::StatusCode::CANCELLED)) {
      const std::string kind = request.kind == PendingRequest::Kind::kEvent ? "event" : "data file";
      LOG_WARN("append recording " + kind + " failed error=" + status.error_message());
    }
  }
}

}  // namespace recording
}  // namespace cockpit
