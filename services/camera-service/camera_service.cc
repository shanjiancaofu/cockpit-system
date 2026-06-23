#include "camera_service.h"

#include <utility>

namespace cockpit {
namespace camera {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

CameraService::CameraService()
    : CameraService([](std::string* error) {
        return V4l2Camera::ListDevices(error);
      }) {
}

CameraService::CameraService(DeviceLister device_lister)
    : device_lister_(std::move(device_lister)) {
}

std::vector<VideoDeviceInfo> CameraService::ListDevices(std::string* error) const {
  return device_lister_(error);
}

bool CameraService::StartPreview(const CameraStartPreviewRequest& request, std::string* error) {
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

  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = CameraPreviewState::kRunning;
  status_.device = request.device;
  status_.width = request.width;
  status_.height = request.height;
  status_.fps = request.fps;
  status_.frames_received = 0;
  status_.frames_dropped = 0;
  status_.last_error.clear();
  return true;
}

void CameraService::StopPreview() {
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

void CameraService::SetError(std::string error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = CameraPreviewState::kFaulted;
  status_.last_error = std::move(error);
}

}  // namespace camera
}  // namespace cockpit
