#include <iostream>
#include <string>

#include "camera_control_client.h"

#include "cockpit/core/runtime/ServiceRuntime.h"
#include "tools/diagnostics/cli_output.h"

namespace {

int Finish(const cockpit::runtime::ServiceRuntime& runtime, int result) {
  runtime.MarkStopped();
  return result;
}

void PrintUsage() {
  std::cout << "camera-ctl [--list|--status|--start|--stop|--photo] "
               "[--device /dev/video0] [--width 640] [--height 480] [--fps 30] "
               "[--filename snapshot.jpg] "
               "[--output text|json] [--config configs/config.yaml]\n";
}

const char* StateName(cockpit::proto::camera::CameraPreviewState state) {
  switch (state) {
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_STOPPED:
      return "stopped";
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_RUNNING:
      return "running";
    case cockpit::proto::camera::CAMERA_PREVIEW_STATE_RECOVERING:
      return "recovering";
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

void PrintStatusText(const cockpit::proto::camera::CameraStatus& status) {
  std::cout << "state: " << StateName(status.state()) << '\n'
            << "device: " << status.device() << '\n'
            << "format: " << status.width() << 'x' << status.height() << " @ " << status.fps()
            << " fps\n"
            << "frames received: " << status.frames_received() << '\n'
            << "frames dropped: " << status.frames_dropped() << '\n'
            << "source frames skipped: " << status.source_frames_skipped() << '\n'
            << "last frame sequence: " << status.last_frame_sequence() << '\n'
            << "last frame timestamp ms: " << status.last_frame_timestamp_ms() << '\n'
            << "last frame received at ms: " << status.last_frame_received_at_ms() << '\n'
            << "restart count: " << status.restart_count() << '\n'
            << "recover count: " << status.recover_count() << '\n'
            << "last recover at ms: " << status.last_recover_at_ms() << '\n';
  if (!status.last_error_kind().empty()) {
    std::cout << "last error kind: " << status.last_error_kind() << '\n';
  }
  if (!status.last_error().empty()) {
    std::cout << "last error: " << status.last_error() << '\n';
  }
}

bool PrintMessage(const google::protobuf::Message& message,
                  cockpit::diagnostics::OutputFormat format, std::string* error) {
  if (format == cockpit::diagnostics::OutputFormat::kJson) {
    return cockpit::diagnostics::WriteJson(message, &std::cout, error);
  }
  return true;
}

int PrintError(const cockpit::runtime::ServiceRuntime& runtime,
               cockpit::diagnostics::OutputFormat format, const std::string& error) {
  const std::string message = error.empty() ? "camera control request failed" : error;
  if (format == cockpit::diagnostics::OutputFormat::kJson) {
    cockpit::diagnostics::WriteJsonError("operation_failed", message, &std::cerr);
  } else {
    std::cerr << message << '\n';
  }
  return Finish(runtime,
                cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed));
}

int PrintArgumentError(const cockpit::runtime::ServiceRuntime& runtime,
                       cockpit::diagnostics::OutputFormat format, const std::string& message) {
  if (format == cockpit::diagnostics::OutputFormat::kJson) {
    cockpit::diagnostics::WriteJsonError("invalid_arguments", message, &std::cerr);
  } else {
    std::cerr << message << '\n';
  }
  return Finish(runtime,
                cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments));
}

}  // namespace

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "camera-ctl");
  cockpit::diagnostics::OutputFormat output_format;
  std::string error;
  if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                               &output_format, &error)) {
    std::cerr << error << '\n';
    return Finish(runtime,
                  cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments));
  }
  if (runtime.args().HasFlag("help")) {
    PrintUsage();
    return Finish(runtime, 0);
  }

  const bool list = runtime.args().HasFlag("list");
  const bool start = runtime.args().HasFlag("start");
  const bool stop = runtime.args().HasFlag("stop");
  const bool photo = runtime.args().HasFlag("photo");
  const bool status_command = runtime.args().HasFlag("status");
  const int command_count = static_cast<int>(list) + static_cast<int>(start) +
                            static_cast<int>(stop) + static_cast<int>(photo) +
                            static_cast<int>(status_command);
  if (command_count > 1) {
    return PrintArgumentError(runtime, output_format, "select exactly one camera command");
  }

  const auto& config = runtime.config().services().camera;
  cockpit::camera::CameraControlClient client(config.grpc.listen_address);

  if (list) {
    cockpit::proto::camera::ListCameraDevicesResponse response;
    if (!client.ListDevices(&response, &error)) {
      return PrintError(runtime, output_format, error);
    }
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      if (!PrintMessage(response, output_format, &error)) {
        return PrintError(runtime, output_format, error);
      }
      return Finish(runtime, 0);
    }
    if (response.devices().empty()) {
      std::cout << "no V4L2 video devices found\n";
    }
    for (const auto& device : response.devices()) {
      PrintDevice(device);
    }
    return Finish(runtime, 0);
  }

  if (start) {
    const int width = runtime.args().GetInt("width", 640);
    const int height = runtime.args().GetInt("height", 480);
    const int fps = runtime.args().GetInt("fps", 30);
    if (width <= 0 || height <= 0 || fps <= 0) {
      return PrintArgumentError(runtime, output_format,
                                "camera width, height, and fps must be positive");
    }
    cockpit::proto::camera::StartPreviewRequest request;
    request.set_device(runtime.args().GetString("device", "/dev/video0"));
    request.set_width(static_cast<std::uint32_t>(width));
    request.set_height(static_cast<std::uint32_t>(height));
    request.set_fps(static_cast<std::uint32_t>(fps));
    cockpit::proto::camera::CameraStatus status;
    if (!client.StartPreview(request, &status, &error)) {
      if (output_format == cockpit::diagnostics::OutputFormat::kText) {
        PrintStatusText(status);
      }
      return PrintError(runtime, output_format, error);
    }
    if (!PrintMessage(status, output_format, &error)) {
      return PrintError(runtime, output_format, error);
    }
    if (output_format == cockpit::diagnostics::OutputFormat::kText) {
      PrintStatusText(status);
    }
    return Finish(runtime, 0);
  }

  if (stop) {
    cockpit::proto::camera::CameraStatus status;
    if (!client.StopPreview(&status, &error)) {
      return PrintError(runtime, output_format, error);
    }
    if (!PrintMessage(status, output_format, &error)) {
      return PrintError(runtime, output_format, error);
    }
    if (output_format == cockpit::diagnostics::OutputFormat::kText) {
      PrintStatusText(status);
    }
    return Finish(runtime, 0);
  }

  if (photo) {
    cockpit::proto::camera::TakePhotoResponse response;
    if (!client.TakePhoto(runtime.args().GetString("filename", ""), &response, &error)) {
      return PrintError(runtime, output_format, error);
    }
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      if (!PrintMessage(response, output_format, &error)) {
        return PrintError(runtime, output_format, error);
      }
      return Finish(runtime, 0);
    }
    std::cout << "photo: " << response.path() << '\n'
              << "frame sequence: " << response.frame_sequence() << '\n'
              << "frame timestamp ms: " << response.frame_timestamp_ms() << '\n'
              << "size: " << response.width() << 'x' << response.height() << '\n'
              << "bytes: " << response.size_bytes() << '\n';
    return Finish(runtime, 0);
  }

  cockpit::proto::camera::CameraStatus status;
  if (!client.GetStatus(&status, &error)) {
    return PrintError(runtime, output_format, error);
  }
  if (!PrintMessage(status, output_format, &error)) {
    return PrintError(runtime, output_format, error);
  }
  if (output_format == cockpit::diagnostics::OutputFormat::kText) {
    PrintStatusText(status);
  }
  return Finish(runtime, 0);
}
