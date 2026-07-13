#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "camera.grpc.pb.h"
#include "cockpit/core/base/macros.h"
#include "cockpit/library/driver/camera/control/camera_service.h"
#include "cockpit/library/driver/camera/photo/camera_photo_service.h"
#include "cockpit/modules/recording/client/recording_event_publisher.h"

namespace cockpit {
namespace camera {

class CameraGrpcService final : public proto::camera::CameraControl::Service {
 public:
  CameraGrpcService(CameraService& camera_service, CameraPhotoService& photo_service,
                    recording::RecordingEventPublisher* recording_events = nullptr);
  ~CameraGrpcService() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(CameraGrpcService);

  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status ListDevices(grpc::ServerContext* context, const proto::common::Empty* request,
                           proto::camera::ListCameraDevicesResponse* response) override;
  grpc::Status StartPreview(grpc::ServerContext* context,
                            const proto::camera::StartPreviewRequest* request,
                            proto::camera::CameraStatus* response) override;
  grpc::Status StopPreview(grpc::ServerContext* context, const proto::common::Empty* request,
                           proto::camera::CameraStatus* response) override;
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::camera::CameraStatus* response) override;
  grpc::Status TakePhoto(grpc::ServerContext* context,
                         const proto::camera::TakePhotoRequest* request,
                         proto::camera::TakePhotoResponse* response) override;

  static void FillDevice(const VideoDeviceInfo& device, proto::camera::CameraDevice* response);
  static void FillStatus(const CameraServiceStatus& status, proto::camera::CameraStatus* response);

  CameraService& camera_service_;
  CameraPhotoService& photo_service_;
  recording::RecordingEventPublisher* recording_events_ = nullptr;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace camera
}  // namespace cockpit
