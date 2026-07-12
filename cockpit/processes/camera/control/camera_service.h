#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cockpit/core/base/macros.h"
#include "cockpit/core/event/message_bus.h"
#include "cockpit/core/runtime/module_manager.h"
#include "cockpit/drivers/v4l2/v4l2_camera.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"
#include "cockpit/modules/camera/frames/camera_frame_sink.h"
#include "cockpit/processes/camera/preview/camera_preview_module.h"

namespace cockpit {
namespace camera {

enum class CameraPreviewState {
  kStopped,
  kRunning,
  kRecovering,
  kFaulted,
};

struct CameraServiceStatus {
  CameraPreviewState state = CameraPreviewState::kStopped;
  std::string device;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fps = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_dropped = 0;
  std::uint64_t source_frames_skipped = 0;
  std::uint64_t last_frame_sequence = 0;
  std::uint64_t last_frame_timestamp_ms = 0;
  std::uint64_t last_frame_received_at_ms = 0;
  std::uint64_t preview_started_at_ms = 0;
  std::uint64_t consecutive_frame_drops = 0;
  std::uint64_t max_consecutive_frame_drops = 0;
  std::uint64_t consecutive_source_gaps = 0;
  std::uint64_t max_consecutive_source_gaps = 0;
  std::string last_error_kind;
  std::uint64_t restart_count = 0;
  std::uint64_t recover_count = 0;
  std::uint64_t last_recover_at_ms = 0;
  std::vector<runtime::ModuleStatus> modules;
  std::string last_error;
};

struct CameraStartPreviewRequest {
  std::string device = "/dev/video0";
  std::uint32_t width = 640;
  std::uint32_t height = 480;
  std::uint32_t fps = 30;
};

struct CameraServiceOptions {
  std::uint64_t preview_stale_timeout_ms = 2000;
};

class CameraService {
 public:
  using DeviceLister = std::function<std::vector<VideoDeviceInfo>(std::string*)>;

  CameraService();
  explicit CameraService(std::shared_ptr<CameraFrameSink> frame_sink);
  CameraService(std::shared_ptr<CameraFrameSink> frame_sink,
                std::shared_ptr<event::MessageBus> message_bus);
  CameraService(std::shared_ptr<CameraFrameSink> frame_sink,
                std::shared_ptr<event::MessageBus> message_bus, CameraServiceOptions options);
  CameraService(DeviceLister device_lister, std::unique_ptr<CameraPreviewSource> preview_source,
                std::shared_ptr<CameraFrameSink> frame_sink = nullptr);
  CameraService(DeviceLister device_lister, std::unique_ptr<CameraPreviewSource> preview_source,
                std::shared_ptr<CameraFrameSink> frame_sink,
                std::shared_ptr<event::MessageBus> message_bus, CameraServiceOptions options = {});
  ~CameraService();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(CameraService);

  std::vector<VideoDeviceInfo> ListDevices(std::string* error) const;
  bool StartPreview(const CameraStartPreviewRequest& request, std::string* error);
  void StopPreview();
  void CheckPreviewHealth();
  CameraServiceStatus status() const;

 private:
  bool DeviceExists(const std::string& device, std::string* error) const;
  void HandleFrame(CameraFrame frame);
  void SetError(std::string kind, std::string error);
  void PublishStatusEvent(const CameraServiceStatus& status) const;
  void PublishFrameEvent(const CameraFrame& frame, std::uint64_t received_at_ms) const;

  DeviceLister device_lister_;
  runtime::ModuleManager module_manager_;
  CameraPreviewModule* preview_module_{nullptr};
  std::shared_ptr<CameraFrameSink> frame_sink_;
  std::shared_ptr<event::MessageBus> message_bus_;
  const CameraServiceOptions options_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::mutex mutex_;
  CameraServiceStatus status_;
  std::chrono::steady_clock::time_point preview_started_steady_;
  std::chrono::steady_clock::time_point last_frame_received_steady_;
};

}  // namespace camera
}  // namespace cockpit
