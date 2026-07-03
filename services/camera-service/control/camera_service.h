#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/runtime/ModuleManager.h"
#include "drivers/v4l2/v4l2_camera.h"
#include "modules/camera/capture/camera_preview_source.h"
#include "modules/camera/frames/camera_frame_sink.h"
#include "services/camera-service/preview/camera_preview_module.h"

namespace cockpit {
namespace camera {

enum class CameraPreviewState {
  kStopped,
  kRunning,
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
  std::vector<runtime::ModuleStatus> modules;
  std::string last_error;
};

struct CameraStartPreviewRequest {
  std::string device = "/dev/video0";
  std::uint32_t width = 640;
  std::uint32_t height = 480;
  std::uint32_t fps = 30;
};

class CameraService {
 public:
  using DeviceLister = std::function<std::vector<VideoDeviceInfo>(std::string*)>;

  CameraService();
  explicit CameraService(std::shared_ptr<CameraFrameSink> frame_sink);
  CameraService(DeviceLister device_lister, std::unique_ptr<CameraPreviewSource> preview_source,
                std::shared_ptr<CameraFrameSink> frame_sink = nullptr);
  ~CameraService();

  CameraService(const CameraService&) = delete;
  CameraService& operator=(const CameraService&) = delete;

  std::vector<VideoDeviceInfo> ListDevices(std::string* error) const;
  bool StartPreview(const CameraStartPreviewRequest& request, std::string* error);
  void StopPreview();
  CameraServiceStatus status() const;

 private:
  static bool IsUsableCaptureDevice(const VideoDeviceInfo& device);
  bool DeviceExists(const std::string& device, std::string* error) const;
  void HandleFrame(CameraFrame frame);
  void SetError(std::string error);

  DeviceLister device_lister_;
  runtime::ModuleManager module_manager_;
  CameraPreviewModule* preview_module_{nullptr};
  std::shared_ptr<CameraFrameSink> frame_sink_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::mutex mutex_;
  CameraServiceStatus status_;
};

}  // namespace camera
}  // namespace cockpit
