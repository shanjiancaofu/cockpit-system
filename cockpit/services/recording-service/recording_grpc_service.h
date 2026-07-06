#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "cockpit/services/recording-service/recording_service.h"
#include "recording.grpc.pb.h"

namespace cockpit {
namespace recording {

class RecordingGrpcService final : public proto::recording::RecordingControl::Service {
 public:
  explicit RecordingGrpcService(RecordingService& recording_service);
  ~RecordingGrpcService() override;

  bool Listen(const std::string& address);
  void Shutdown();

 private:
  grpc::Status Start(grpc::ServerContext* context,
                     const proto::recording::StartRecordingRequest* request,
                     proto::recording::RecordingStatus* response) override;
  grpc::Status Stop(grpc::ServerContext* context, const proto::common::Empty* request,
                    proto::recording::RecordingStatus* response) override;
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::recording::RecordingStatus* response) override;

  static void FillStatus(const RecordingStatus& status,
                         proto::recording::RecordingStatus* response);

  RecordingService& recording_service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace recording
}  // namespace cockpit
