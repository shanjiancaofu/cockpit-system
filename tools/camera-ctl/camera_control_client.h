#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "camera.grpc.pb.h"

namespace cockpit {
namespace camera {

class CameraControlClient {
 public:
  explicit CameraControlClient(const std::string& address);

  bool ListDevices(proto::camera::ListCameraDevicesResponse* response, std::string* error);
  bool StartPreview(const proto::camera::StartPreviewRequest& request,
                    proto::camera::CameraStatus* status, std::string* error);
  bool StopPreview(proto::camera::CameraStatus* status, std::string* error);
  bool GetStatus(proto::camera::CameraStatus* status, std::string* error);

 private:
  static void SetDeadline(grpc::ClientContext* context);

  std::unique_ptr<proto::camera::CameraControl::Stub> stub_;
};

}  // namespace camera
}  // namespace cockpit
