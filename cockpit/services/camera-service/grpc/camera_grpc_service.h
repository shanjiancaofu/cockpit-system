#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "camera.grpc.pb.h"
#include "cockpit/services/camera-service/control/camera_service.h"

namespace cockpit {
namespace camera {

class CameraGrpcService final : public proto::camera::CameraControl::Service {
 public:
  explicit CameraGrpcService(CameraService& camera_service);
  ~CameraGrpcService() override;

  CameraGrpcService(const CameraGrpcService&) = delete;
  CameraGrpcService& operator=(const CameraGrpcService&) = delete;

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

  static void FillDevice(const VideoDeviceInfo& device, proto::camera::CameraDevice* response);
  static void FillStatus(const CameraServiceStatus& status, proto::camera::CameraStatus* response);

  CameraService& camera_service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace camera
}  // namespace cockpit
