#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "drivers/v4l2/v4l2_camera.h"
#include "modules/camera/camera_preview_source.h"

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
  CameraService(DeviceLister device_lister, std::unique_ptr<CameraPreviewSource> preview_source);
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
  void HandleFrame(const CameraFrame& frame);
  void SetError(std::string error);

  DeviceLister device_lister_;
  std::unique_ptr<CameraPreviewSource> preview_source_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::mutex mutex_;
  CameraServiceStatus status_;
};

}  // namespace camera
}  // namespace cockpit
