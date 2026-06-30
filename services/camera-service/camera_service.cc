#include "camera_service.h"

#include <utility>

#include "modules/camera/latest_frame_buffer.h"

#if defined(COCKPIT_HAS_GSTREAMER_CAMERA)
#include "modules/camera/gstreamer_preview_pipeline.h"
#endif

namespace cockpit {
namespace camera {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::unique_ptr<CameraPreviewSource> CreateDefaultPreviewSource() {
#if defined(COCKPIT_HAS_GSTREAMER_CAMERA)
  return std::make_unique<GstreamerPreviewPipeline>();
#else
  return nullptr;
#endif
}

}  // namespace

CameraService::CameraService()
    : CameraService(
          [](std::string* error) {
            return V4l2Camera::ListDevices(error);
          },
          CreateDefaultPreviewSource()) {
}

CameraService::CameraService(DeviceLister device_lister,
                             std::unique_ptr<CameraPreviewSource> preview_source,
                             std::shared_ptr<CameraFrameSink> frame_sink)
    : device_lister_(std::move(device_lister)),
      preview_source_(std::move(preview_source)),
      frame_sink_(frame_sink == nullptr ? std::make_shared<LatestFrameBuffer>()
                                        : std::move(frame_sink)) {
}

CameraService::~CameraService() {
  StopPreview();
}

std::vector<VideoDeviceInfo> CameraService::ListDevices(std::string* error) const {
  return device_lister_(error);
}

bool CameraService::StartPreview(const CameraStartPreviewRequest& request, std::string* error) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (preview_source_ != nullptr) {
    preview_source_->Stop();
  }
  if (request.device.empty()) {
    AssignError(error, "camera device must not be empty");
    SetError("camera device must not be empty");
    return false;
  }
  if (request.width == 0 || request.height == 0 || request.fps == 0) {
    AssignError(error, "camera preview width, height, and fps must be positive");
    SetError("camera preview width, height, and fps must be positive");
    return false;
  }
  if (!DeviceExists(request.device, error)) {
    SetError(error == nullptr ? "camera device is not available" : *error);
    return false;
  }
  if (preview_source_ == nullptr) {
    AssignError(error, "camera preview backend is not available");
    SetError("camera preview backend is not available");
    return false;
  }

  CameraPreviewConfig config;
  config.device = request.device;
  config.width = request.width;
  config.height = request.height;
  config.fps = request.fps;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = CameraPreviewState::kStopped;
    status_.device = request.device;
    status_.width = request.width;
    status_.height = request.height;
    status_.fps = request.fps;
    status_.frames_received = 0;
    status_.frames_dropped = 0;
    status_.last_error.clear();
  }

  std::string start_error;
  if (!preview_source_->Start(
          config,
          [this](CameraFrame frame) {
            HandleFrame(std::move(frame));
          },
          &start_error)) {
    if (start_error.empty()) {
      start_error = "start camera preview backend failed";
    }
    AssignError(error, start_error);
    SetError(start_error);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = CameraPreviewState::kRunning;
  }
  return true;
}

void CameraService::StopPreview() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (preview_source_ != nullptr) {
    preview_source_->Stop();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = CameraPreviewState::kStopped;
}

CameraServiceStatus CameraService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

bool CameraService::IsUsableCaptureDevice(const VideoDeviceInfo& device) {
  return device.query_ok && device.supports_capture && device.supports_streaming;
}

bool CameraService::DeviceExists(const std::string& device, std::string* error) const {
  std::string list_error;
  const auto devices = ListDevices(&list_error);
  for (const auto& info : devices) {
    if (info.path == device && IsUsableCaptureDevice(info)) {
      return true;
    }
  }
  if (!list_error.empty() && devices.empty()) {
    AssignError(error, list_error);
  } else {
    AssignError(error, "camera device is not available for capture: " + device);
  }
  return false;
}

void CameraService::HandleFrame(CameraFrame frame) {
  if (!frame.IsValid() || frame_sink_ == nullptr || !frame_sink_->Publish(std::move(frame))) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++status_.frames_dropped;
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  ++status_.frames_received;
}

void CameraService::SetError(std::string error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = CameraPreviewState::kFaulted;
  status_.last_error = std::move(error);
}

}  // namespace camera
}  // namespace cockpit
