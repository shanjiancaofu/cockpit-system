#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "cockpit/core/event/message_bus.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/service_runtime.h"
#include "cockpit/modules/camera/capture/synthetic_preview_source.h"
#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"
#include "cockpit/modules/recording/client/recording_event_publisher.h"
#include "cockpit/services/camera-service/control/camera_service.h"
#include "cockpit/services/camera-service/grpc/camera_grpc_service.h"
#include "cockpit/services/camera-service/photo/camera_photo_service.h"
#include "cockpit/services/camera-service/recording_bridge.h"

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
  auto message_bus = std::make_shared<cockpit::event::MessageBus>();
  cockpit::camera::CameraServiceOptions camera_options;
  camera_options.preview_stale_timeout_ms =
      static_cast<std::uint64_t>(service_config.preview_stale_timeout_ms);
  std::unique_ptr<cockpit::camera::CameraService> camera_service;
  if (service_config.capture_backend == "synthetic") {
    cockpit::camera::SyntheticCameraOptions synthetic_options;
    synthetic_options.fault =
        cockpit::camera::ParseSyntheticCameraFault(service_config.synthetic_fault);
    synthetic_options.fault_after_frames =
        static_cast<std::uint64_t>(service_config.synthetic_fault_after_frames);
    camera_service = std::make_unique<cockpit::camera::CameraService>(
        [](std::string*) {
          cockpit::camera::VideoDeviceInfo device;
          device.path = "synthetic://camera0";
          device.driver = "cockpit-synthetic";
          device.card = "Cockpit Synthetic Camera";
          device.bus_info = "in-process";
          device.query_ok = true;
          device.supports_capture = true;
          device.supports_streaming = true;
          return std::vector<cockpit::camera::VideoDeviceInfo>{std::move(device)};
        },
        std::make_unique<cockpit::camera::SyntheticPreviewSource>(synthetic_options),
        std::move(frame_sink), message_bus, camera_options);
  } else {
    camera_service = std::make_unique<cockpit::camera::CameraService>(std::move(frame_sink),
                                                                      message_bus, camera_options);
  }
  std::filesystem::path photo_directory(service_config.photo_directory);
  if (photo_directory.is_relative()) {
    photo_directory = std::filesystem::path(runtime.config().paths().data_dir) / photo_directory;
  }
  cockpit::camera::CameraPhotoService photo_service(
      service_config.shared_memory_name, photo_directory, service_config.photo_jpeg_quality,
      service_config.photo_max_frame_age_ms);
  cockpit::recording::RecordingEventPublisher recording_events(
      runtime.config().services().recording.grpc.listen_address);
  auto camera_events = message_bus->Subscribe("*");
  std::atomic<bool> bridge_running{true};
  std::thread recording_bridge([&]() {
    cockpit::camera::CameraRecordingBridgeFilter bridge_filter;
    while (bridge_running.load()) {
      auto message = camera_events->WaitPopFor(std::chrono::milliseconds(100));
      if (!message.has_value()) {
        continue;
      }
      if (!bridge_filter.ShouldForward(*message)) {
        continue;
      }
      recording_events.Publish(message->timestamp_ms, message->topic, message->payload_json);
    }
  });
  cockpit::camera::CameraGrpcService grpc_service(*camera_service, photo_service,
                                                  &recording_events);

  if (!grpc_service.Start(service_config.grpc.listen_address)) {
    bridge_running.store(false);
    camera_events->Close();
    message_bus->Close();
    if (recording_bridge.joinable()) {
      recording_bridge.join();
    }
    runtime.MarkStopped();
    return 1;
  }

  while (!runtime.ShouldStop()) {
    camera_service->CheckPreviewHealth();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  grpc_service.Shutdown();
  camera_service->StopPreview();
  bridge_running.store(false);
  camera_events->Close();
  message_bus->Close();
  if (recording_bridge.joinable()) {
    recording_bridge.join();
  }
  runtime.MarkStopped();
  return 0;
}
