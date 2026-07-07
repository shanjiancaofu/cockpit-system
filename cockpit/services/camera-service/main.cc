#include <chrono>
#include <filesystem>
#include <thread>

#include "cockpit/core/logging/Logger.h"
#include "cockpit/core/runtime/ServiceRuntime.h"
#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"
#include "cockpit/services/camera-service/control/camera_service.h"
#include "cockpit/services/camera-service/grpc/camera_grpc_service.h"
#include "cockpit/services/camera-service/photo/camera_photo_service.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "camera-service");
  const auto& service_config = runtime.config().services().camera;
  cockpit::camera::SharedFrameBufferConfig frame_config;
  frame_config.name = service_config.shared_memory_name;
  frame_config.max_frame_bytes = static_cast<std::size_t>(service_config.max_frame_bytes);
  std::string frame_error;
  auto frame_writer = cockpit::camera::SharedFrameWriter::Create(frame_config, &frame_error);
  if (frame_writer == nullptr) {
    LOG_ERROR(frame_error);
    runtime.MarkStopped();
    return 1;
  }
  std::shared_ptr<cockpit::camera::CameraFrameSink> frame_sink(std::move(frame_writer));
  cockpit::camera::CameraService camera_service(std::move(frame_sink));
  std::filesystem::path photo_directory(service_config.photo_directory);
  if (photo_directory.is_relative()) {
    photo_directory = std::filesystem::path(runtime.config().paths().data_dir) / photo_directory;
  }
  cockpit::camera::CameraPhotoService photo_service(
      service_config.shared_memory_name, photo_directory, service_config.photo_jpeg_quality,
      service_config.photo_max_frame_age_ms);
  cockpit::camera::CameraGrpcService grpc_service(camera_service, photo_service);

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
