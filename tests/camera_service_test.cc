#include "services/camera-service/camera_service.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

cockpit::camera::VideoDeviceInfo CaptureDevice(const std::string& path) {
  cockpit::camera::VideoDeviceInfo device;
  device.path = path;
  device.driver = "uvcvideo";
  device.card = "USB Camera";
  device.bus_info = "usb-test";
  device.query_ok = true;
  device.supports_capture = true;
  device.supports_streaming = true;
  return device;
}

cockpit::camera::VideoDeviceInfo MetadataDevice(const std::string& path) {
  cockpit::camera::VideoDeviceInfo device;
  device.path = path;
  device.driver = "uvcvideo";
  device.card = "USB Camera Metadata";
  device.bus_info = "usb-test";
  device.query_ok = true;
  device.supports_capture = false;
  device.supports_streaming = true;
  return device;
}

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  int list_calls = 0;
  cockpit::camera::CameraService service([&list_calls](std::string*) {
    ++list_calls;
    return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video0"),
                                                         MetadataDevice("/dev/video1")};
  });

  std::string error;
  const auto devices = service.ListDevices(&error);
  if (!Check(devices.size() == 2, "camera service did not list devices") ||
      !Check(error.empty(), "camera service returned unexpected list error")) {
    return 1;
  }

  cockpit::camera::CameraStartPreviewRequest request;
  request.device = "/dev/video0";
  request.width = 1280;
  request.height = 720;
  request.fps = 30;
  if (!Check(service.StartPreview(request, &error), "camera preview start failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  auto status = service.status();
  if (!Check(status.state == cockpit::camera::CameraPreviewState::kRunning,
             "camera preview did not enter running state") ||
      !Check(status.device == "/dev/video0", "camera preview device mismatch") ||
      !Check(status.width == 1280 && status.height == 720 && status.fps == 30,
             "camera preview config mismatch") ||
      !Check(status.last_error.empty(), "camera preview kept stale error")) {
    return 1;
  }

  request.device = "/dev/video1";
  if (!Check(!service.StartPreview(request, &error), "metadata-only camera start was accepted") ||
      !Check(error.find("not available") != std::string::npos,
             "metadata-only camera error message is invalid")) {
    return 1;
  }
  status = service.status();
  if (!Check(status.state == cockpit::camera::CameraPreviewState::kFaulted,
             "camera service did not enter faulted state after invalid start")) {
    return 1;
  }

  service.StopPreview();
  status = service.status();
  if (!Check(status.state == cockpit::camera::CameraPreviewState::kStopped,
             "camera preview did not stop") ||
      !Check(list_calls >= 3, "camera service did not use injected device lister")) {
    return 1;
  }

  std::cout << "camera service tests passed\n";
  return 0;
}
