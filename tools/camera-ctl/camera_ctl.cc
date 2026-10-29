#include "camera_ctl.h"

#include <iostream>
#include <string>

#include "camera_control_client.h"

#include "cockpit/exchange/foundation/runtime/process_runtime.h"
#include "tools/diagnostics/cli_output.h"

namespace {

void PrintUsage() {
  std::cout << "camera-ctl [--list|--status|--start|--stop|--photo] "
               "[--device /dev/video0|nvargus://0] [--width 640] [--height 480] [--fps 30] "
               "[--h264-output file.h264 --h264-width 1280 --h264-height 720] "
               "[--h264-udp-host 127.0.0.1 --h264-udp-port 8556] "
               "[--filename snapshot.jpg] "
               "[--output text|json] [--config configs/development.yaml]\n";
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
  if (status.native_metrics_valid()) {
    std::cout << "native frames delivered: " << status.native_frames_delivered() << '\n'
              << "native delivery p95 ms: " << status.native_delivery_p95_ms() << '\n'
              << "native capture acquired: " << status.native_capture_frames_acquired() << '\n'
              << "native capture returned: " << status.native_capture_leases_returned() << '\n'
              << "native capture released: " << status.native_capture_buffers_released() << '\n'
              << "native broker budget dropped: " << status.native_broker_budget_dropped() << '\n'
              << "native broker retained: " << status.native_broker_retained_frames() << '\n'
              << "native VIC transformed: " << status.native_vic_transformed() << '\n'
              << "native VIC total ms: " << status.native_vic_total_ms() << '\n'
              << "native encoder frames: " << status.native_encoder_frames_submitted() << '\n'
              << "native encoder packets: " << status.native_encoder_packets() << '\n'
              << "native encoder keyframes: " << status.native_encoder_keyframes() << '\n'
              << "native encoder bytes: " << status.native_encoder_bytes() << '\n';
  }
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

int PrintError(const cockpit::runtime::ProcessRuntime& runtime,
               cockpit::diagnostics::OutputFormat format, const std::string& error) {
  static_cast<void>(runtime);
  const std::string message = error.empty() ? "camera control request failed" : error;
  if (format == cockpit::diagnostics::OutputFormat::kJson) {
    cockpit::diagnostics::WriteJsonError("operation_failed", message, &std::cerr);
  } else {
    std::cerr << message << '\n';
  }
  return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed);
}

int PrintArgumentError(const cockpit::runtime::ProcessRuntime& runtime,
                       cockpit::diagnostics::OutputFormat format, const std::string& message) {
  static_cast<void>(runtime);
  if (format == cockpit::diagnostics::OutputFormat::kJson) {
    cockpit::diagnostics::WriteJsonError("invalid_arguments", message, &std::cerr);
  } else {
    std::cerr << message << '\n';
  }
  return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
}

}  // namespace

int cockpit::camera_ctl::ControlCamera(const cockpit::runtime::ProcessRuntime& runtime) {
  cockpit::diagnostics::OutputFormat output_format;
  std::string error;
  if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                               &output_format, &error)) {
    std::cerr << error << '\n';
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  if (runtime.args().HasFlag("help")) {
    PrintUsage();
    return 0;
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
      return 0;
    }
    if (response.devices().empty()) {
      std::cout << "no V4L2 video devices found\n";
    }
    for (const auto& device : response.devices()) {
      PrintDevice(device);
    }
    return 0;
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
    request.set_h264_output_path(runtime.args().GetString("h264-output", ""));
    const int h264_width = runtime.args().GetInt("h264-width", 1280);
    const int h264_height = runtime.args().GetInt("h264-height", 720);
    const int h264_bitrate = runtime.args().GetInt("h264-bitrate", 2'000'000);
    const int h264_gop = runtime.args().GetInt("h264-gop", 30);
    if (h264_width <= 0 || h264_height <= 0 || h264_bitrate <= 0 || h264_gop <= 0) {
      return PrintArgumentError(runtime, output_format,
                                "H264 dimensions, bitrate, and GOP must be positive");
    }
    request.set_h264_width(static_cast<std::uint32_t>(h264_width));
    request.set_h264_height(static_cast<std::uint32_t>(h264_height));
    request.set_h264_bitrate(static_cast<std::uint32_t>(h264_bitrate));
    request.set_h264_gop(static_cast<std::uint32_t>(h264_gop));
    request.set_h264_udp_host(runtime.args().GetString("h264-udp-host", ""));
    const int h264_udp_port = runtime.args().GetInt("h264-udp-port", 0);
    if (h264_udp_port < 0 || h264_udp_port > 65535) {
      return PrintArgumentError(runtime, output_format, "H264 UDP port must be between 0 and 65535");
    }
    request.set_h264_udp_port(static_cast<std::uint32_t>(h264_udp_port));
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
    return 0;
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
    return 0;
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
      return 0;
    }
    std::cout << "photo: " << response.path() << '\n'
              << "frame sequence: " << response.frame_sequence() << '\n'
              << "frame timestamp ms: " << response.frame_timestamp_ms() << '\n'
              << "size: " << response.width() << 'x' << response.height() << '\n'
              << "bytes: " << response.size_bytes() << '\n';
    return 0;
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
  return 0;
}
