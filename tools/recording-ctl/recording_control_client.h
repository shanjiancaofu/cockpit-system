#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "recording.grpc.pb.h"

namespace cockpit {
namespace recording {

class RecordingControlClient {
 public:
  explicit RecordingControlClient(const std::string& address);

  bool Start(const std::string& trigger, proto::recording::RecordingStatus* status,
             std::string* error);
  bool Stop(proto::recording::RecordingStatus* status, std::string* error);
  bool AppendEvent(std::int64_t timestamp_ms, const std::string& topic,
                   const std::string& payload_json, proto::recording::RecordingStatus* status,
                   std::string* error);
  bool AppendDataFile(const proto::recording::AppendRecordingDataFileRequest& request,
                      proto::recording::RecordingStatus* status, std::string* error);
  bool GetStatus(proto::recording::RecordingStatus* status, std::string* error);
  bool List(std::uint32_t limit, proto::recording::ListRecordingsResponse* response,
            std::string* error);
  bool GetDetail(const std::string& session_id, proto::recording::RecordingSessionDetail* response,
                 std::string* error);
  bool GetTimeline(const proto::recording::GetRecordingTimelineRequest& request,
                   proto::recording::GetRecordingTimelineResponse* response, std::string* error);
  bool Verify(const std::string& session_id, proto::recording::VerifyRecordingResponse* response,
              std::string* error);
  bool Delete(const std::string& session_id, std::string* error);
  bool Prune(proto::recording::PruneRecordingsResponse* response, std::string* error);

 private:
  static void SetDeadline(grpc::ClientContext* context);

  std::unique_ptr<proto::recording::RecordingControl::Stub> stub_;
};

}  // namespace recording
}  // namespace cockpit
