#include "services/camera-service/camera_service.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
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

class FakePreviewSource : public cockpit::camera::CameraPreviewSource {
 public:
  bool Start(const cockpit::camera::CameraPreviewConfig& config, FrameCallback callback,
             std::string*) override {
    config_ = config;
    running_ = true;
    ++start_count_;

    cockpit::camera::CameraFrame frame;
    frame.width = config.width;
    frame.height = config.height;
    frame.stride_bytes = config.width * 4;
    frame.format = cockpit::camera::CameraPixelFormat::kBgrx;
    frame.data.resize(static_cast<std::size_t>(frame.stride_bytes) * frame.height);
    callback(frame);
    return true;
  }

  void Stop() override {
    running_ = false;
    ++stop_count_;
  }

  bool IsRunning() const override {
    return running_;
  }

  const cockpit::camera::CameraPreviewConfig& config() const {
    return config_;
  }

  int start_count() const {
    return start_count_;
  }

  int stop_count() const {
    return stop_count_;
  }

 private:
  cockpit::camera::CameraPreviewConfig config_;
  bool running_ = false;
  int start_count_ = 0;
  int stop_count_ = 0;
};

}  // namespace

int main() {
  int list_calls = 0;
  auto preview_source = std::make_unique<FakePreviewSource>();
  auto* preview_source_ptr = preview_source.get();
  cockpit::camera::CameraService service(
      [&list_calls](std::string*) {
        ++list_calls;
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video0"),
                                                             MetadataDevice("/dev/video1")};
      },
      std::move(preview_source));

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
      !Check(status.frames_received == 1, "camera preview frame was not counted") ||
      !Check(preview_source_ptr->start_count() == 1, "camera preview source was not started") ||
      !Check(preview_source_ptr->config().device == "/dev/video0",
             "camera preview source device mismatch") ||
      !Check(status.modules.size() == 1 && status.modules[0].name == "camera-preview" &&
                 status.modules[0].state == cockpit::runtime::ModuleState::kRunning,
             "camera preview module status mismatch") ||
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
      !Check(preview_source_ptr->stop_count() >= 1, "camera preview source was not stopped") ||
      !Check(status.modules.size() == 1 &&
                 status.modules[0].state == cockpit::runtime::ModuleState::kStopped,
             "camera preview module did not stop") ||
      !Check(list_calls >= 3, "camera service did not use injected device lister")) {
    return 1;
  }

  std::cout << "camera service tests passed\n";
  return 0;
}
