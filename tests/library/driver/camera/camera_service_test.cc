#include "cockpit/library/driver/camera/control/camera_service.h"

#include <linux/videodev2.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cockpit/core/event/message_bus.h"
#include "cockpit/library/driver/camera/recording_bridge.h"

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

cockpit::camera::VideoDeviceInfo JetsonCsiDevice(const std::string& path) {
  auto device = CaptureDevice(path);
  device.driver = "tegra-video";
  device.card = "vi-output, imx219";
  device.bus_info = "platform:tegra-capture-vi:1";
  return device;
}

std::vector<cockpit::camera::PixelFormatInfo> UvcMjpegFormats(const std::string&, std::string*) {
  cockpit::camera::PixelFormatInfo format;
  format.fourcc = V4L2_PIX_FMT_MJPEG;
  format.fourcc_text = "MJPG";
  format.frame_sizes = {{1280, 720}};
  return {format};
}

std::vector<cockpit::camera::PixelFormatInfo> UvcMjpegUnknownSizes(const std::string&,
                                                                   std::string*) {
  cockpit::camera::PixelFormatInfo format;
  format.fourcc = V4L2_PIX_FMT_MJPEG;
  format.fourcc_text = "MJPG";
  return {format};
}

std::vector<cockpit::camera::PixelFormatInfo> UvcYuyvFormats(const std::string&, std::string*) {
  cockpit::camera::PixelFormatInfo format;
  format.fourcc = V4L2_PIX_FMT_YUYV;
  format.fourcc_text = "YUYV";
  return {format};
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
  cockpit::camera::CameraPreviewConfig invalid_pipeline_config;
  invalid_pipeline_config.width = 640;
  invalid_pipeline_config.height = 480;
  invalid_pipeline_config.fps = 30;
  const auto discard_frame = [](cockpit::camera::CameraFrame) {
  };
  std::string pipeline_error;
  auto argus_source =
      cockpit::camera::CreateCameraPreviewSource(cockpit::camera::CameraCapturePipeline::kArgusIsp,
                                                 cockpit::camera::CameraUvcInputFormat::kMjpeg);
  invalid_pipeline_config.device = "/dev/video0";
  if (!Check(argus_source != nullptr &&
                 !argus_source->Start(invalid_pipeline_config, discard_frame, &pipeline_error),
             "Argus pipeline accepted a V4L2 path")) {
    return 1;
  }
  auto uvc_source = cockpit::camera::CreateCameraPreviewSource(
      cockpit::camera::CameraCapturePipeline::kUvc, cockpit::camera::CameraUvcInputFormat::kMjpeg);
  invalid_pipeline_config.device = "nvargus://0";
  if (!Check(uvc_source != nullptr &&
                 !uvc_source->Start(invalid_pipeline_config, discard_frame, &pipeline_error),
             "UVC pipeline accepted an Argus URI")) {
    return 1;
  }
  auto software_isp_source = cockpit::camera::CreateCameraPreviewSource(
      cockpit::camera::CameraCapturePipeline::kSoftwareIsp,
      cockpit::camera::CameraUvcInputFormat::kMjpeg);
  if (!Check(
          software_isp_source != nullptr &&
              !software_isp_source->Start(invalid_pipeline_config, discard_frame, &pipeline_error),
          "Software ISP pipeline accepted an Argus URI")) {
    return 1;
  }

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

  cockpit::camera::CameraService jetson_device_service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{JetsonCsiDevice("/dev/video0"),
                                                             JetsonCsiDevice("/dev/video1"),
                                                             CaptureDevice("/dev/video2")};
      },
      std::make_unique<FakePreviewSource>(), nullptr);
  const auto jetson_devices = jetson_device_service.ListDevices(nullptr);
  if (!Check(jetson_devices.size() == 2, "Jetson CSI device count mismatch") ||
      !Check(jetson_devices[0].path == "nvargus://0" && jetson_devices[1].path == "nvargus://1",
             "Jetson CSI devices were not mapped to Argus sensor IDs") ||
      !Check(jetson_devices[0].driver == "nvargus",
             "Jetson CSI device did not expose the Argus driver")) {
    return 1;
  }

  cockpit::camera::CameraServiceOptions software_isp_options;
  software_isp_options.capture_pipeline = cockpit::camera::CameraCapturePipeline::kSoftwareIsp;
  cockpit::camera::CameraService software_isp_device_service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video0"),
                                                             JetsonCsiDevice("/dev/video2")};
      },
      std::make_unique<FakePreviewSource>(), nullptr, nullptr, software_isp_options);
  const auto software_isp_devices = software_isp_device_service.ListDevices(nullptr);
  if (!Check(software_isp_devices.size() == 1 && software_isp_devices[0].path == "/dev/video2" &&
                 software_isp_devices[0].driver == "tegra-video",
             "Software ISP pipeline did not isolate the tegra-video device")) {
    return 1;
  }

  int synthetic_list_calls = 0;
  cockpit::camera::CameraServiceOptions synthetic_options;
  synthetic_options.capture_pipeline = cockpit::camera::CameraCapturePipeline::kSynthetic;
  cockpit::camera::CameraService synthetic_service(
      [&synthetic_list_calls](std::string*) {
        ++synthetic_list_calls;
        return std::vector<cockpit::camera::VideoDeviceInfo>{};
      },
      std::make_unique<FakePreviewSource>(), nullptr, nullptr, synthetic_options);
  const auto synthetic_devices = synthetic_service.ListDevices(nullptr);
  cockpit::camera::CameraStartPreviewRequest synthetic_request;
  synthetic_request.device = "synthetic://0";
  std::string synthetic_error;
  if (!Check(synthetic_devices.size() == 1 && synthetic_devices[0].path == "synthetic://0",
             "Synthetic pipeline did not expose its virtual device") ||
      !Check(synthetic_list_calls == 0,
             "Synthetic pipeline unexpectedly enumerated physical devices") ||
      !Check(synthetic_service.StartPreview(synthetic_request, &synthetic_error),
             "Headless synthetic preview did not start")) {
    std::cerr << synthetic_error << '\n';
    return 1;
  }
  synthetic_service.StopPreview();

  cockpit::camera::CameraServiceOptions calibrated_options;
  calibrated_options.capture_pipeline = cockpit::camera::CameraCapturePipeline::kSynthetic;
  cockpit::hawkeye::CameraCalibration verified_calibration;
  verified_calibration.image_width = 1920;
  verified_calibration.image_height = 1080;
  verified_calibration.fx = 2500.0;
  verified_calibration.fy = 2500.0;
  calibrated_options.calibration = verified_calibration;
  cockpit::camera::CameraService calibrated_service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{};
      },
      std::make_unique<FakePreviewSource>(), nullptr, nullptr, calibrated_options);
  cockpit::camera::CameraStartPreviewRequest mismatched_calibration_request;
  mismatched_calibration_request.device = "synthetic://0";
  mismatched_calibration_request.width = 1280;
  mismatched_calibration_request.height = 720;
  std::string calibration_error;
  if (!Check(!calibrated_service.StartPreview(mismatched_calibration_request, &calibration_error),
             "verified calibration accepted a mismatched preview resolution") ||
      !Check(calibration_error.find("does not match verified calibration") != std::string::npos,
             "calibration resolution mismatch did not report a deterministic error")) {
    return 1;
  }

  int list_calls = 0;
  auto preview_source = std::make_unique<FakePreviewSource>();
  auto* preview_source_ptr = preview_source.get();
  auto message_bus = std::make_shared<cockpit::event::MessageBus>();
  auto camera_status_events = message_bus->Subscribe("/camera/status");
  auto camera_frame_events = message_bus->Subscribe("/camera/frame_meta");
  cockpit::camera::CameraServiceOptions uvc_options;
  uvc_options.capture_pipeline = cockpit::camera::CameraCapturePipeline::kUvc;
  cockpit::camera::CameraService mismatched_uvc_service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video9")};
      },
      std::make_unique<FakePreviewSource>(), nullptr, nullptr, uvc_options, UvcYuyvFormats);
  cockpit::camera::CameraStartPreviewRequest mismatched_uvc_request;
  mismatched_uvc_request.device = "/dev/video9";
  std::string mismatched_uvc_error;
  if (!Check(!mismatched_uvc_service.StartPreview(mismatched_uvc_request, &mismatched_uvc_error),
             "MJPEG UVC pipeline accepted a YUYV-only device") ||
      !Check(mismatched_uvc_error.find("configured pipeline format") != std::string::npos,
             "UVC format mismatch did not report a deterministic error")) {
    return 1;
  }
  cockpit::camera::CameraService mismatched_uvc_size_service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video9")};
      },
      std::make_unique<FakePreviewSource>(), nullptr, nullptr, uvc_options, UvcMjpegFormats);
  cockpit::camera::CameraStartPreviewRequest mismatched_uvc_size_request;
  mismatched_uvc_size_request.device = "/dev/video9";
  mismatched_uvc_size_request.width = 1920;
  mismatched_uvc_size_request.height = 1080;
  std::string mismatched_uvc_size_error;
  if (!Check(!mismatched_uvc_size_service.StartPreview(mismatched_uvc_size_request,
                                                       &mismatched_uvc_size_error),
             "UVC pipeline accepted an unsupported frame size") ||
      !Check(mismatched_uvc_size_error.find("frame size") != std::string::npos,
             "UVC frame-size mismatch did not report a deterministic error")) {
    return 1;
  }
  cockpit::camera::CameraService unknown_uvc_size_service(
      [](std::string*) {
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video9")};
      },
      std::make_unique<FakePreviewSource>(), nullptr, nullptr, uvc_options, UvcMjpegUnknownSizes);
  cockpit::camera::CameraStartPreviewRequest unknown_uvc_size_request;
  unknown_uvc_size_request.device = "/dev/video9";
  unknown_uvc_size_request.width = 1280;
  unknown_uvc_size_request.height = 720;
  std::string unknown_uvc_size_error;
  if (!Check(
          unknown_uvc_size_service.StartPreview(unknown_uvc_size_request, &unknown_uvc_size_error),
          "UVC pipeline rejected a format with unknown frame-size capabilities")) {
    std::cerr << unknown_uvc_size_error << '\n';
    return 1;
  }
  unknown_uvc_size_service.StopPreview();
  cockpit::camera::CameraService service(
      [&list_calls](std::string*) {
        ++list_calls;
        return std::vector<cockpit::camera::VideoDeviceInfo>{CaptureDevice("/dev/video0"),
                                                             MetadataDevice("/dev/video1")};
      },
      std::move(preview_source), nullptr, message_bus, uvc_options, UvcMjpegFormats);

  std::string error;
  const auto devices = service.ListDevices(&error);
  if (!Check(devices.size() == 1 && devices[0].path == "/dev/video0",
             "UVC pipeline did not filter metadata-only devices") ||
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

  request.device = "nvargus://0";
  if (!Check(!service.StartPreview(request, &error), "UVC pipeline accepted an Argus device") ||
      !Check(error.find("not available") != std::string::npos,
             "UVC/Argus mismatch did not fail closed")) {
    return 1;
  }

  request.device = "/dev/video0";
  if (!Check(service.StartPreview(request, &error), "UVC preview did not recover after mismatch")) {
    std::cerr << error << '\n';
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

  std::atomic<bool> read_status{true};
  std::thread status_reader([&service, &read_status]() {
    while (read_status.load()) {
      static_cast<void>(service.status());
    }
  });
  for (int i = 0; i < 50; ++i) {
    if (!service.StartPreview(request, &error)) {
      read_status = false;
      status_reader.join();
      std::cerr << "concurrent camera lifecycle start failed: " << error << '\n';
      return 1;
    }
    service.StopPreview();
  }
  read_status = false;
  status_reader.join();

  std::cout << "camera service tests passed\n";
  return 0;
}
