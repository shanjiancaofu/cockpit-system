#include "cockpit/services/camera-service/control/camera_service.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cockpit/core/event/message_bus.h"
#include "cockpit/services/camera-service/recording_bridge.h"

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
    callback_ = std::move(callback);
    running_ = true;
    ++start_count_;

    EmitFrame(1, 1000);
    return true;
  }

  void EmitFrame(std::uint64_t sequence, std::uint64_t timestamp_ms) {
    cockpit::camera::CameraFrame frame;
    frame.sequence = sequence;
    frame.timestamp_ms = timestamp_ms;
    frame.width = config_.width;
    frame.height = config_.height;
    frame.stride_bytes = config_.width * 4;
    frame.format = cockpit::camera::CameraPixelFormat::kBgrx;
    frame.data.resize(static_cast<std::size_t>(frame.stride_bytes) * frame.height);
    callback_(std::move(frame));
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
  FrameCallback callback_;
  bool running_ = false;
  int start_count_ = 0;
  int stop_count_ = 0;
};

}  // namespace

int main() {
  cockpit::camera::CameraRecordingBridgeFilter bridge_filter;
  cockpit::event::EventMessage status_message{
      "/camera/status", "camera.status", "camera-service", "{}", 100, 0};
  cockpit::event::EventMessage frame_message{
      "/camera/frame_meta", "camera.frame_meta", "camera-service", "{}", 100, 0};
  cockpit::event::EventMessage vehicle_message{
      "/vehicle/state", "vehicle.state", "vehicle-data-service", "{}", 100, 0};
  if (!Check(bridge_filter.ShouldForward(status_message), "camera bridge did not forward status") ||
      !Check(!bridge_filter.ShouldForward(vehicle_message),
             "camera bridge forwarded non-camera event") ||
      !Check(bridge_filter.ShouldForward(frame_message),
             "camera bridge did not forward first frame meta")) {
    return 1;
  }
  for (int i = 0; i < 29; ++i) {
    if (!Check(!bridge_filter.ShouldForward(frame_message),
               "camera bridge frame meta sampling mismatch")) {
      return 1;
    }
  }
  if (!Check(bridge_filter.ShouldForward(frame_message),
             "camera bridge did not forward sampled frame meta")) {
    return 1;
  }

  int list_calls = 0;
  auto preview_source = std::make_unique<FakePreviewSource>();
  auto* preview_source_ptr = preview_source.get();
  auto message_bus = std::make_shared<cockpit::event::MessageBus>();
  auto camera_status_events = message_bus->Subscribe("/camera/status");
  auto camera_frame_events = message_bus->Subscribe("/camera/frame_meta");
  cockpit::camera::CameraService service(
      [&list_calls](std::string*) {
        ++list_calls;
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video0"),
                                                             MetadataDevice("/dev/video1")};
      },
      std::move(preview_source), nullptr, message_bus);

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
      !Check(status.source_frames_skipped == 0, "camera preview reported an initial frame gap") ||
      !Check(status.last_frame_sequence == 1, "camera preview sequence metric mismatch") ||
      !Check(status.last_frame_timestamp_ms == 1000, "camera preview timestamp metric mismatch") ||
      !Check(status.last_frame_received_at_ms > 0,
             "camera preview receive time was not recorded") ||
      !Check(status.preview_started_at_ms > 0, "camera preview start time was not recorded") ||
      !Check(preview_source_ptr->start_count() == 1, "camera preview source was not started") ||
      !Check(preview_source_ptr->config().device == "/dev/video0",
             "camera preview source device mismatch") ||
      !Check(status.modules.size() == 1 && status.modules[0].name == "camera-preview" &&
                 status.modules[0].state == cockpit::runtime::ModuleState::kRunning,
             "camera preview module status mismatch") ||
      !Check(status.last_error.empty(), "camera preview kept stale error")) {
    return 1;
  }

  const auto first_status_event = camera_status_events->TryPop();
  const auto first_frame_event = camera_frame_events->TryPop();
  if (!Check(first_status_event.has_value(), "camera status event was not published") ||
      !Check(first_frame_event.has_value(), "camera frame metadata event was not published") ||
      !Check(first_frame_event->payload_json.find("\"sequence\":1") != std::string::npos,
             "camera frame event payload mismatch")) {
    return 1;
  }

  preview_source_ptr->EmitFrame(4, 1100);
  status = service.status();
  if (!Check(status.frames_received == 2, "second camera preview frame was not counted") ||
      !Check(status.source_frames_skipped == 2, "camera source frame gap metric mismatch") ||
      !Check(status.consecutive_source_gaps == 1, "camera source gap streak mismatch") ||
      !Check(status.max_consecutive_source_gaps == 1, "camera max source gap mismatch") ||
      !Check(status.last_frame_sequence == 4, "latest camera frame sequence mismatch") ||
      !Check(status.last_frame_timestamp_ms == 1100, "latest camera frame timestamp mismatch")) {
    return 1;
  }

  if (!Check(service.StartPreview(request, &error), "camera preview restart failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  status = service.status();
  if (!Check(status.state == cockpit::camera::CameraPreviewState::kRunning,
             "camera preview did not run after restart") ||
      !Check(status.restart_count == 1, "camera restart count mismatch") ||
      !Check(preview_source_ptr->start_count() == 2, "camera preview source was not restarted")) {
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
             "camera service did not enter faulted state after invalid start") ||
      !Check(status.last_error_kind == "device_unavailable",
             "camera service did not classify device error")) {
    return 1;
  }

  request.device = "/dev/video0";
  if (!Check(service.StartPreview(request, &error), "camera preview recovery failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  status = service.status();
  if (!Check(status.state == cockpit::camera::CameraPreviewState::kRunning,
             "camera preview did not run after recovery") ||
      !Check(status.recover_count == 1, "camera recover count mismatch") ||
      !Check(status.last_recover_at_ms > 0, "camera recover timestamp missing") ||
      !Check(status.last_error_kind.empty(), "camera recovery kept stale error kind") ||
      !Check(status.last_error.empty(), "camera recovery kept stale error")) {
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
