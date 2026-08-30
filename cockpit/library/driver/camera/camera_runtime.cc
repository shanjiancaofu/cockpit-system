#include "cockpit/library/driver/camera/camera_runtime.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/event/message_bus.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/drivers/v4l2/v4l2_camera.h"
#include "cockpit/library/driver/camera/control/camera_service.h"
#include "cockpit/library/driver/camera/grpc/camera_grpc_service.h"
#include "cockpit/library/driver/camera/photo/camera_photo_service.h"
#include "cockpit/library/driver/camera/recording_bridge.h"
#include "cockpit/modules/camera/capture/synthetic_preview_source.h"
#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"
#include "cockpit/modules/hawkeye/camera_calibration_loader.h"
#include "cockpit/modules/hawkeye/camera_info.h"
#include "cockpit/modules/recording/client/recording_event_publisher.h"

namespace cockpit {
namespace camera {

class CameraRuntime::Impl {
 public:
  std::shared_ptr<event::MessageBus> message_bus;
  std::unique_ptr<CameraService> service;
  std::unique_ptr<CameraPhotoService> photo_service;
  std::unique_ptr<recording::RecordingEventPublisher> recording_events;
  std::shared_ptr<event::MessageSubscription> camera_events;
  std::unique_ptr<CameraGrpcService> grpc;
  std::atomic_bool bridge_running{false};
  std::thread bridge;
};

CameraRuntime::CameraRuntime() = default;

CameraRuntime::~CameraRuntime() {
  Stop();
}

bool CameraRuntime::Start(const std::string& config_path) {
  if (impl_ != nullptr) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    const auto& camera_config = config.services().camera;
    logging::InitLogger("camera_driver", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);

    SharedFrameBufferConfig frame_config;
    frame_config.name = camera_config.shared_memory_name;
    frame_config.max_frame_bytes = static_cast<std::size_t>(camera_config.max_frame_bytes);
    std::string error;
    auto frame_writer = SharedFrameWriter::Create(frame_config, &error);
    if (frame_writer == nullptr) {
      LOG_ERROR(error);
      return false;
    }
    std::shared_ptr<CameraFrameSink> frame_sink(std::move(frame_writer));

    impl_ = std::make_unique<Impl>();
    impl_->message_bus = std::make_shared<event::MessageBus>();
    CameraServiceOptions camera_options;
    camera_options.preview_stale_timeout_ms =
        static_cast<std::uint64_t>(camera_config.preview_stale_timeout_ms);
    camera_options.capture_pipeline = ParseCameraCapturePipeline(camera_config.capture_pipeline);
    camera_options.uvc_input_format = ParseCameraUvcInputFormat(camera_config.uvc_input_format);
    camera_options.calibration_pipeline = camera_config.calibration_pipeline;
    camera_options.calibration_device = camera_config.calibration_device;
    if (!camera_config.calibration_file.empty()) {
      hawkeye::CameraCalibration calibration;
      if (!hawkeye::CameraCalibrationLoader::LoadFromFile(camera_config.calibration_file,
                                                          &calibration, &error)) {
        LOG_ERROR(error);
        return false;
      }
      hawkeye::CameraInfo camera_info;
      if (!hawkeye::ToCameraInfo(calibration, &camera_info, &error)) {
        LOG_ERROR(error);
        return false;
      }
      camera_options.calibration = calibration;
    }
    if (camera_options.capture_pipeline == CameraCapturePipeline::kSynthetic) {
      SyntheticCameraOptions synthetic_options;
      synthetic_options.fault = ParseSyntheticCameraFault(camera_config.synthetic_fault);
      synthetic_options.fault_after_frames =
          static_cast<std::uint64_t>(camera_config.synthetic_fault_after_frames);
      impl_->service = std::make_unique<CameraService>(
          [](std::string*) {
            return std::vector<VideoDeviceInfo>{};
          },
          std::make_unique<SyntheticPreviewSource>(synthetic_options), std::move(frame_sink),
          impl_->message_bus, camera_options);
    } else {
      auto preview_source = CreateCameraPreviewSource(camera_options.capture_pipeline,
                                                      camera_options.uvc_input_format);
      if (preview_source == nullptr) {
        LOG_ERROR("camera capture pipeline has no preview source");
        return false;
      }
      impl_->service = std::make_unique<CameraService>(
          [](std::string* list_error) {
            return V4l2Camera::ListDevices(list_error);
          },
          std::move(preview_source), std::move(frame_sink), impl_->message_bus, camera_options);
    }

    std::filesystem::path photo_directory(camera_config.photo_directory);
    if (photo_directory.is_relative()) {
      photo_directory = std::filesystem::path(config.paths().data_dir) / photo_directory;
    }
    impl_->photo_service = std::make_unique<CameraPhotoService>(
        camera_config.shared_memory_name, photo_directory, camera_config.photo_jpeg_quality,
        camera_config.photo_max_frame_age_ms);
    impl_->recording_events = std::make_unique<recording::RecordingEventPublisher>(
        config.services().recording.grpc.listen_address);
    impl_->camera_events = impl_->message_bus->Subscribe("*");
    impl_->bridge_running.store(true);
    impl_->bridge = std::thread([this] {
      CameraRecordingBridgeFilter filter;
      while (impl_->bridge_running.load()) {
        auto message = impl_->camera_events->WaitPopFor(std::chrono::milliseconds(100));
        if (message.has_value() && filter.ShouldForward(*message)) {
          impl_->recording_events->Publish(message->timestamp_ms, message->topic,
                                           message->payload_json);
        }
      }
    });
    impl_->grpc = std::make_unique<CameraGrpcService>(*impl_->service, *impl_->photo_service,
                                                      impl_->recording_events.get());
    if (!impl_->grpc->Start(camera_config.grpc.listen_address)) {
      Stop();
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure camera driver: " + std::string(error.what()));
    Stop();
    return false;
  }
}

void CameraRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->grpc != nullptr) {
    impl_->grpc->Shutdown();
  }
  if (impl_->service != nullptr) {
    impl_->service->StopPreview();
  }
  impl_->bridge_running.store(false);
  if (impl_->camera_events != nullptr) {
    impl_->camera_events->Close();
  }
  if (impl_->message_bus != nullptr) {
    impl_->message_bus->Close();
  }
  if (impl_->bridge.joinable()) {
    impl_->bridge.join();
  }
  impl_.reset();
}

int CameraRuntime::Poll() {
  if (impl_ == nullptr) {
    return 1;
  }
  impl_->service->CheckPreviewHealth();
  return impl_->service->status().state == CameraPreviewState::kFaulted ? 1 : 0;
}

}  // namespace camera
}  // namespace cockpit
