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
  grpc::Status AppendEvent(grpc::ServerContext* context,
                           const proto::recording::AppendRecordingEventRequest* request,
                           proto::recording::RecordingStatus* response) override;
  grpc::Status AppendDataFile(grpc::ServerContext* context,
                              const proto::recording::AppendRecordingDataFileRequest* request,
                              proto::recording::RecordingStatus* response) override;
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::recording::RecordingStatus* response) override;
  grpc::Status List(grpc::ServerContext* context,
                    const proto::recording::ListRecordingsRequest* request,
                    proto::recording::ListRecordingsResponse* response) override;
  grpc::Status GetDetail(grpc::ServerContext* context,
                         const proto::recording::GetRecordingDetailRequest* request,
                         proto::recording::RecordingSessionDetail* response) override;
  grpc::Status GetTimeline(grpc::ServerContext* context,
                           const proto::recording::GetRecordingTimelineRequest* request,
                           proto::recording::GetRecordingTimelineResponse* response) override;
  grpc::Status Delete(grpc::ServerContext* context,
                      const proto::recording::DeleteRecordingRequest* request,
                      proto::common::Empty* response) override;
  grpc::Status Prune(grpc::ServerContext* context, const proto::common::Empty* request,
                     proto::recording::PruneRecordingsResponse* response) override;

  static void FillStatus(const RecordingStatus& status,
                         proto::recording::RecordingStatus* response);
  static void FillSession(const RecordingSessionInfo& session,
                          proto::recording::RecordingSessionInfo* response);
  static void FillDetail(const RecordingSessionDetail& detail,
                         proto::recording::RecordingSessionDetail* response);

  RecordingService& recording_service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace recording
}  // namespace cockpit
