#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/library/driver/camera/control/camera_service.h"
#include "cockpit/modules/camera/capture/synthetic_preview_source.h"

namespace {

cockpit::camera::VideoDeviceInfo SyntheticDevice() {
  cockpit::camera::VideoDeviceInfo device;
  device.path = "synthetic://camera0";
  device.query_ok = true;
  device.supports_capture = true;
  device.supports_streaming = true;
  return device;
}

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool WaitForFault(cockpit::camera::CameraService* service, const std::string& expected_kind) {
  for (int attempt = 0; attempt < 40; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    service->CheckPreviewHealth();
    const auto status = service->status();
    if (status.state == cockpit::camera::CameraPreviewState::kFaulted) {
      return status.last_error_kind == expected_kind;
    }
  }
  return false;
}

bool WaitForFrames(cockpit::camera::CameraService* service, std::uint64_t minimum_frames) {
  for (int attempt = 0; attempt < 40; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    service->CheckPreviewHealth();
    if (service->status().frames_received >= minimum_frames) {
      return true;
    }
  }
  return false;
}

bool TestFault(cockpit::camera::SyntheticCameraFault fault, std::uint64_t fault_after_frames,
               const std::string& expected_kind) {
  cockpit::camera::SyntheticCameraOptions synthetic_options;
  synthetic_options.fault = fault;
  synthetic_options.fault_after_frames = fault_after_frames;
  auto source = std::make_unique<cockpit::camera::SyntheticPreviewSource>(synthetic_options);
  auto* source_ptr = source.get();
  cockpit::camera::CameraServiceOptions service_options;
  service_options.preview_stale_timeout_ms = 40;
  service_options.capture_pipeline = cockpit::camera::CameraCapturePipeline::kSynthetic;
  cockpit::camera::CameraService service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{SyntheticDevice()};
      },
      std::move(source), nullptr, nullptr, service_options);

  cockpit::camera::CameraStartPreviewRequest request;
  request.device = "synthetic://camera0";
  request.width = 64;
  request.height = 48;
  request.fps = 100;
  std::string error;
  if (!Check(service.StartPreview(request, &error), "synthetic preview start failed") ||
      !Check(WaitForFault(&service, expected_kind), "synthetic fault was not classified")) {
    std::cerr << error << '\n';
    return false;
  }

  source_ptr->SetFault(cockpit::camera::SyntheticCameraFault::kNone, 0);
  if (!Check(service.StartPreview(request, &error), "synthetic preview recovery failed") ||
      !Check(WaitForFrames(&service, 2), "synthetic preview did not recover frames")) {
    std::cerr << error << '\n';
    return false;
  }
  auto status = service.status();
  if (!Check(status.recover_count == 1, "synthetic recover count mismatch") ||
      !Check(status.last_recover_at_ms > 0, "synthetic recover timestamp missing") ||
      !Check(status.last_error_kind.empty(), "synthetic recovery kept stale error")) {
    return false;
  }

  if (!Check(service.StartPreview(request, &error), "synthetic preview restart failed") ||
      !Check(WaitForFrames(&service, 2), "synthetic restart did not publish frames")) {
    std::cerr << error << '\n';
    return false;
  }
  status = service.status();
  service.StopPreview();
  return Check(status.restart_count == 1, "synthetic restart count mismatch");
}

}  // namespace

int main() {
  const bool result =
      TestFault(cockpit::camera::SyntheticCameraFault::kNoFrames, 0, "no_frames") &&
      TestFault(cockpit::camera::SyntheticCameraFault::kStall, 2, "frame_stalled") &&
      TestFault(cockpit::camera::SyntheticCameraFault::kDisconnect, 2, "source_disconnected");
  return result ? 0 : 1;
}
