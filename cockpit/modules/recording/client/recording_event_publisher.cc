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

}  // namespace

RecordingEventPublisher::RecordingEventPublisher(const std::string& address)
    : stub_(proto::recording::RecordingControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))),
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
    if (!status.ok() && !recording_inactive &&
        !(stopping && status.error_code() == grpc::StatusCode::CANCELLED)) {
      const std::string kind = request.kind == PendingRequest::Kind::kEvent ? "event" : "data file";
      LOG_WARN("append recording " + kind + " failed error=" + status.error_message());
    }
  }
}

}  // namespace recording
}  // namespace cockpit
