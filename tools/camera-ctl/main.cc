#include <iostream>
#include <string>

#include "camera_control_client.h"

#include "cockpit/core/runtime/ServiceRuntime.h"

namespace {

int Finish(const cockpit::runtime::ServiceRuntime& runtime, int result) {
  runtime.MarkStopped();
  return result;
}

void PrintUsage() {
  std::cout << "camera-ctl [--list|--status|--start|--stop] "
               "[--device /dev/video0] [--width 640] [--height 480] [--fps 30] "
               "[--config configs/config.yaml]\n";
}

const char* StateName(cockpit::proto::camera::CameraPreviewState state) {
  switch (state) {
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_STOPPED:
      return "stopped";
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_RUNNING:
      return "running";
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_FAULTED:
      return "faulted";
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_UNSPECIFIED:
      return "unspecified";
    default:
      return "unknown";
  }
}

void PrintDevice(const cockpit::proto::camera::CameraDevice& device) {
  if (!device.query_ok()) {
    std::cout << device.path() << " query=failed error=\"" << device.error() << "\"\n";
    return;
  }
  std::cout << device.path() << " card=\"" << device.card() << "\""
            << " driver=\"" << device.driver() << "\""
            << " bus=\"" << device.bus_info() << "\""
            << " capture=" << (device.supports_capture() ? "yes" : "no")
            << " streaming=" << (device.supports_streaming() ? "yes" : "no") << '\n';
}

void PrintStatus(const cockpit::proto::camera::CameraStatus& status) {
  std::cout << "state: " << StateName(status.state()) << '\n'
            << "device: " << status.device() << '\n'
            << "format: " << status.width() << 'x' << status.height() << " @ " << status.fps()
            << " fps\n"
            << "frames received: " << status.frames_received() << '\n'
            << "frames dropped: " << status.frames_dropped() << '\n'
            << "source frames skipped: " << status.source_frames_skipped() << '\n'
            << "last frame sequence: " << status.last_frame_sequence() << '\n'
            << "last frame timestamp ms: " << status.last_frame_timestamp_ms() << '\n'
            << "last frame received at ms: " << status.last_frame_received_at_ms() << '\n';
  if (!status.last_error().empty()) {
    std::cout << "last error: " << status.last_error() << '\n';
  }
}

int PrintError(const cockpit::runtime::ServiceRuntime& runtime, const std::string& error) {
  std::cerr << (error.empty() ? "camera control request failed" : error) << '\n';
  return Finish(runtime, 1);
}

}  // namespace

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "camera-ctl");
  if (runtime.args().HasFlag("help")) {
    PrintUsage();
    return Finish(runtime, 0);
  }

  const auto& config = runtime.config().services().camera;
  cockpit::camera::CameraControlClient client(config.grpc.listen_address);
  std::string error;

  if (runtime.args().HasFlag("list")) {
    cockpit::proto::camera::ListCameraDevicesResponse response;
    if (!client.ListDevices(&response, &error)) {
      return PrintError(runtime, error);
    }
    if (response.devices().empty()) {
      std::cout << "no V4L2 video devices found\n";
    }
    for (const auto& device : response.devices()) {
      PrintDevice(device);
    }
    return Finish(runtime, 0);
  }

  if (runtime.args().HasFlag("start")) {
    cockpit::proto::camera::StartPreviewRequest request;
    request.set_device(runtime.args().GetString("device", "/dev/video0"));
    request.set_width(static_cast<std::uint32_t>(runtime.args().GetInt("width", 640)));
    request.set_height(static_cast<std::uint32_t>(runtime.args().GetInt("height", 480)));
    request.set_fps(static_cast<std::uint32_t>(runtime.args().GetInt("fps", 30)));
    cockpit::proto::camera::CameraStatus status;
    if (!client.StartPreview(request, &status, &error)) {
      PrintStatus(status);
      return PrintError(runtime, error);
    }
    PrintStatus(status);
    return Finish(runtime, 0);
  }

  if (runtime.args().HasFlag("stop")) {
    cockpit::proto::camera::CameraStatus status;
    if (!client.StopPreview(&status, &error)) {
      return PrintError(runtime, error);
    }
    PrintStatus(status);
    return Finish(runtime, 0);
  }

  cockpit::proto::camera::CameraStatus status;
  if (!client.GetStatus(&status, &error)) {
    return PrintError(runtime, error);
  }
  PrintStatus(status);
  return Finish(runtime, 0);
}
