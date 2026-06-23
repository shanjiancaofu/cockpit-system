#include <chrono>
#include <thread>

#include "camera_grpc_service.h"
#include "camera_service.h"

#include "core/runtime/ServiceRuntime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "camera-service");
  cockpit::camera::CameraService camera_service;
  cockpit::camera::CameraGrpcService grpc_service(camera_service);

  const auto& service_config = runtime.config().services().camera;
  if (!grpc_service.Start(service_config.grpc.listen_address)) {
    runtime.MarkStopped();
    return 1;
  }

  while (!runtime.ShouldStop()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  grpc_service.Shutdown();
  camera_service.StopPreview();
  runtime.MarkStopped();
  return 0;
}
