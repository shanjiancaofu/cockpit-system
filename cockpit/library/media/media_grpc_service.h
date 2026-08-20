#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "cockpit/core/base/macros.h"
#include "cockpit/library/media/media_service.h"
#include "media.grpc.pb.h"

namespace cockpit {
namespace media {

class MediaGrpcService final : public proto::media::MediaControl::Service {
 public:
  explicit MediaGrpcService(MediaService& service);
  ~MediaGrpcService() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(MediaGrpcService);

  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status ListTracks(grpc::ServerContext* context, const proto::common::Empty* request,
                          proto::media::ListMediaTracksResponse* response) override;
  grpc::Status Play(grpc::ServerContext* context, const proto::media::PlayMediaRequest* request,
                    proto::media::MediaStatus* response) override;
  grpc::Status Pause(grpc::ServerContext* context, const proto::common::Empty* request,
                     proto::media::MediaStatus* response) override;
  grpc::Status Resume(grpc::ServerContext* context, const proto::common::Empty* request,
                      proto::media::MediaStatus* response) override;
  grpc::Status Stop(grpc::ServerContext* context, const proto::common::Empty* request,
                    proto::media::MediaStatus* response) override;
  grpc::Status Next(grpc::ServerContext* context, const proto::common::Empty* request,
                    proto::media::MediaStatus* response) override;
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::media::MediaStatus* response) override;

  static void FillStatus(const MediaPlaybackStatus& status, proto::media::MediaStatus* response);

  MediaService& service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace media
}  // namespace cockpit
