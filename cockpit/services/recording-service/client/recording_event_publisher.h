#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"
#include "recording.grpc.pb.h"

namespace cockpit {
namespace recording {

class RecordingEventPublisher {
 public:
  explicit RecordingEventPublisher(const std::string& address);
  ~RecordingEventPublisher();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(RecordingEventPublisher);

  bool Publish(std::int64_t timestamp_ms, const std::string& topic,
               const std::string& payload_json);
  bool PublishDataFile(proto::recording::AppendRecordingDataFileRequest request);
  void Stop();

 private:
  struct PendingRequest {
    enum class Kind {
      kEvent,
      kDataFile,
    };

    Kind kind = Kind::kEvent;
    proto::recording::AppendRecordingEventRequest event;
    proto::recording::AppendRecordingDataFileRequest data_file;
  };

  bool Enqueue(PendingRequest request);
  void Run();

  static constexpr std::size_t kQueueCapacity = 128U;
  std::unique_ptr<proto::recording::RecordingControl::Stub> stub_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<PendingRequest> queue_;
  bool stopping_ = false;
  grpc::ClientContext* active_context_ = nullptr;
  std::thread worker_;
};

}  // namespace recording
}  // namespace cockpit
