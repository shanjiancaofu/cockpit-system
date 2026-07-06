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
  bool GetStatus(proto::recording::RecordingStatus* status, std::string* error);
  bool List(std::uint32_t limit, proto::recording::ListRecordingsResponse* response,
            std::string* error);
  bool Delete(const std::string& session_id, std::string* error);
  bool Prune(proto::recording::PruneRecordingsResponse* response, std::string* error);

 private:
  static void SetDeadline(grpc::ClientContext* context);

  std::unique_ptr<proto::recording::RecordingControl::Stub> stub_;
};

}  // namespace recording
}  // namespace cockpit
