#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "camera.grpc.pb.h"
#include "cockpit/driver/camera/runtime/control/camera_service.h"
#include "cockpit/driver/camera/runtime/photo/camera_photo_service.h"
#include "cockpit/driver/camera/tuning/tuning_profile.h"
#include "cockpit/exchange/foundation/base/macros.h"
#include "cockpit/spider/recording/client/recording_event_publisher.h"

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
  grpc::Status GetTuning(grpc::ServerContext*, const proto::common::Empty*,
                         proto::camera::CameraTuningResponse*) override;
  grpc::Status SetTuning(grpc::ServerContext*, const proto::camera::CameraTuningRequest*,
                         proto::camera::CameraTuningResponse*) override;
  grpc::Status ResetTuning(grpc::ServerContext*, const proto::camera::CameraTuningRequest*,
                           proto::camera::CameraTuningResponse*) override;
  grpc::Status ApplyTuningProfile(grpc::ServerContext*, const proto::camera::CameraTuningRequest*,
                                  proto::camera::CameraTuningResponse*) override;

  static void FillDevice(const VideoDeviceInfo& device, proto::camera::CameraDevice* response);
  static void FillStatus(const CameraServiceStatus& status, proto::camera::CameraStatus* response);
  grpc::Status FillTuningResponse(const CameraServiceStatus&, const CameraTuningState*,
                                  bool accepted, const std::string&,
                                  proto::camera::CameraTuningResponse*);

  CameraService& camera_service_;
  CameraPhotoService& photo_service_;
  recording::RecordingEventPublisher* recording_events_ = nullptr;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace camera
}  // namespace cockpit
