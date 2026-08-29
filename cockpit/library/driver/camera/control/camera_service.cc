#include "camera_service.h"

#include <linux/videodev2.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "cockpit/core/json/json.h"
#include "cockpit/core/time/time.h"
#include "cockpit/modules/camera/capture/argus_isp_preview_source.h"
#include "cockpit/modules/camera/capture/software_isp_preview_source.h"
#include "cockpit/modules/camera/capture/uvc_preview_source.h"
#include "cockpit/modules/camera/frames/latest_frame_buffer.h"

namespace cockpit {
namespace camera {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::unique_ptr<CameraPreviewSource> CreateDefaultPreviewSource() {
  return std::make_unique<ArgusIspPreviewSource>();
}

}  // namespace

CameraCapturePipeline ParseCameraCapturePipeline(const std::string& value) {
  if (value == "argus_isp") return CameraCapturePipeline::kArgusIsp;
  if (value == "uvc") return CameraCapturePipeline::kUvc;
  if (value == "software_isp") return CameraCapturePipeline::kSoftwareIsp;
  if (value == "synthetic") return CameraCapturePipeline::kSynthetic;
  throw std::invalid_argument("unsupported camera capture pipeline: " + value);
}

CameraUvcInputFormat ParseCameraUvcInputFormat(const std::string& value) {
  if (value == "mjpeg") return CameraUvcInputFormat::kMjpeg;
  if (value == "yuyv") return CameraUvcInputFormat::kYuyv;
  throw std::invalid_argument("unsupported UVC input format: " + value);
}

std::unique_ptr<CameraPreviewSource> CreateCameraPreviewSource(
    CameraCapturePipeline pipeline, CameraUvcInputFormat uvc_input_format) {
  if (pipeline == CameraCapturePipeline::kArgusIsp) {
    return std::make_unique<ArgusIspPreviewSource>();
  }
  if (pipeline == CameraCapturePipeline::kUvc) {
    return std::make_unique<UvcPreviewSource>(uvc_input_format);
  }
  if (pipeline == CameraCapturePipeline::kSoftwareIsp) {
    return std::make_unique<SoftwareIspPreviewSource>();
  }
  return nullptr;
}

namespace {

std::vector<VideoDeviceInfo> NormalizePreviewDevices(std::vector<VideoDeviceInfo> devices,
                                                     CameraCapturePipeline pipeline) {
  if (pipeline == CameraCapturePipeline::kSynthetic) {
    VideoDeviceInfo device;
    device.path = "synthetic://0";
    device.driver = "cockpit-synthetic";
    device.card = "Cockpit Synthetic Camera";
    device.bus_info = "in-process";
    device.query_ok = true;
    device.supports_capture = true;
    device.supports_streaming = true;
    return {std::move(device)};
  }
  std::vector<VideoDeviceInfo> normalized;
  std::uint32_t argus_sensor_id = 0;
  for (auto device : devices) {
    const bool jetson_csi = device.query_ok && device.supports_capture &&
                            device.supports_streaming && device.driver == "tegra-video" &&
                            device.bus_info.rfind("platform:tegra-capture-vi", 0) == 0;
    const bool uvc = device.query_ok && device.supports_capture && device.supports_streaming &&
                     device.driver == "uvcvideo" && device.bus_info.rfind("usb-", 0) == 0;
    if (pipeline == CameraCapturePipeline::kArgusIsp && jetson_csi) {
      device.path = "nvargus://" + std::to_string(argus_sensor_id++);
      device.driver = "nvargus";
      device.card += " (Argus ISP)";
      normalized.push_back(std::move(device));
    } else if (pipeline == CameraCapturePipeline::kUvc && uvc) {
      normalized.push_back(std::move(device));
    } else if (pipeline == CameraCapturePipeline::kSoftwareIsp && jetson_csi) {
      normalized.push_back(std::move(device));
    }
  }
  return normalized;
}

}  // namespace

CameraService::CameraService()
    : CameraService(
          [](std::string* error) {
            return V4l2Camera::ListDevices(error);
          },
          CreateDefaultPreviewSource()) {
}

CameraService::CameraService(std::shared_ptr<CameraFrameSink> frame_sink)
    : CameraService(
          [](std::string* error) {
            return V4l2Camera::ListDevices(error);
          },
          CreateDefaultPreviewSource(), std::move(frame_sink)) {
}

CameraService::CameraService(std::shared_ptr<CameraFrameSink> frame_sink,
                             std::shared_ptr<event::MessageBus> message_bus)
    : CameraService(std::move(frame_sink), std::move(message_bus), {}) {
}

CameraService::CameraService(std::shared_ptr<CameraFrameSink> frame_sink,
                             std::shared_ptr<event::MessageBus> message_bus,
                             CameraServiceOptions options)
    : CameraService(
          [](std::string* error) {
            return V4l2Camera::ListDevices(error);
          },
          CreateDefaultPreviewSource(), std::move(frame_sink), std::move(message_bus), options) {
}

CameraService::CameraService(DeviceLister device_lister,
                             std::unique_ptr<CameraPreviewSource> preview_source,
                             std::shared_ptr<CameraFrameSink> frame_sink)
    : CameraService(std::move(device_lister), std::move(preview_source), std::move(frame_sink),
                    nullptr, {}) {
}

CameraService::CameraService(DeviceLister device_lister,
                             std::unique_ptr<CameraPreviewSource> preview_source,
                             std::shared_ptr<CameraFrameSink> frame_sink,
                             std::shared_ptr<event::MessageBus> message_bus,
                             CameraServiceOptions options, FormatLister format_lister)
    : device_lister_(std::move(device_lister)),
      format_lister_(format_lister ? std::move(format_lister)
                                   : [](const std::string& device, std::string* error) {
                                       return V4l2Camera::ListFormats(device, error);
                                     }),
      frame_sink_(frame_sink == nullptr ? std::make_shared<LatestFrameBuffer>()
                                        : std::move(frame_sink)),
      message_bus_(std::move(message_bus)),
      options_(options) {
  auto preview_module = std::make_unique<CameraPreviewModule>(std::move(preview_source));
  preview_module_ = preview_module.get();
  module_manager_.Add(std::move(preview_module));
}

CameraService::~CameraService() {
  StopPreview();
}

std::vector<VideoDeviceInfo> CameraService::ListDevices(std::string* error) const {
  if (options_.capture_pipeline == CameraCapturePipeline::kSynthetic) {
    return NormalizePreviewDevices({}, options_.capture_pipeline);
  }
  return NormalizePreviewDevices(device_lister_(error), options_.capture_pipeline);
}

bool CameraService::StartPreview(const CameraStartPreviewRequest& request, std::string* error) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  CameraPreviewState previous_state = CameraPreviewState::kStopped;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    previous_state = status_.state;
    if (status_.state == CameraPreviewState::kRunning ||
        status_.state == CameraPreviewState::kFaulted) {
      status_.state = CameraPreviewState::kRecovering;
      if (previous_state == CameraPreviewState::kRunning) {
        ++status_.restart_count;
      }
      PublishStatusEvent(status_);
    }
  }
  module_manager_.StopAll();
  if (request.device.empty()) {
    AssignError(error, "camera device must not be empty");
    SetError("invalid_argument", "camera device must not be empty");
    return false;
  }
  if (request.width == 0 || request.height == 0 || request.fps == 0) {
    AssignError(error, "camera preview width, height, and fps must be positive");
    SetError("invalid_argument", "camera preview width, height, and fps must be positive");
    return false;
  }
  if (!DeviceExists(request.device, request.width, request.height, error)) {
    SetError("device_unavailable", error == nullptr ? "camera device is not available" : *error);
    return false;
  }
  if (preview_module_ == nullptr || !preview_module_->available()) {
    AssignError(error, "camera preview backend is not available");
    SetError("backend_unavailable", "camera preview backend is not available");
    return false;
  }

  CameraPreviewConfig config;
  config.device = request.device;
  config.width = request.width;
  config.height = request.height;
  config.fps = request.fps;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = previous_state == CameraPreviewState::kRunning ||
                            previous_state == CameraPreviewState::kFaulted
                        ? CameraPreviewState::kRecovering
                        : CameraPreviewState::kStopped;
    status_.device = request.device;
    status_.width = request.width;
    status_.height = request.height;
    status_.fps = request.fps;
    status_.frames_received = 0;
    status_.frames_dropped = 0;
    status_.source_frames_skipped = 0;
    status_.last_frame_sequence = 0;
    status_.last_frame_timestamp_ms = 0;
    status_.last_frame_received_at_ms = 0;
    status_.preview_started_at_ms = 0;
    status_.consecutive_frame_drops = 0;
    status_.max_consecutive_frame_drops = 0;
    status_.consecutive_source_gaps = 0;
    status_.max_consecutive_source_gaps = 0;
    preview_started_steady_ = {};
    last_frame_received_steady_ = {};
  }

  preview_module_->Configure(config, [this](CameraFrame frame) {
    HandleFrame(std::move(frame));
  });
  if (!module_manager_.StartAll()) {
    std::string start_error = preview_module_->last_error();
    if (start_error.empty()) {
      start_error = "start camera preview backend failed";
    }
    AssignError(error, start_error);
    SetError("start_failed", start_error);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = CameraPreviewState::kRunning;
    status_.preview_started_at_ms =
        static_cast<std::uint64_t>(time::WallTime::Now().ToMilliseconds());
    preview_started_steady_ = std::chrono::steady_clock::now();
    if (previous_state == CameraPreviewState::kFaulted) {
      ++status_.recover_count;
      status_.last_recover_at_ms = status_.preview_started_at_ms;
    }
    status_.last_error.clear();
    status_.last_error_kind.clear();
    PublishStatusEvent(status_);
  }
  return true;
}

void CameraService::StopPreview() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  module_manager_.StopAll();
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = CameraPreviewState::kStopped;
  PublishStatusEvent(status_);
}

void CameraService::CheckPreviewHealth() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::string error_kind;
  std::string error_message;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != CameraPreviewState::kRunning) {
      return;
    }
    if (preview_module_ == nullptr || !preview_module_->is_running()) {
      error_kind = "source_disconnected";
      error_message = "camera preview source disconnected";
    } else {
      const auto reference =
          status_.frames_received == 0 ? preview_started_steady_ : last_frame_received_steady_;
      if (reference != std::chrono::steady_clock::time_point{}) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - reference)
                                    .count();
        if (elapsed_ms > 0 &&
            static_cast<std::uint64_t>(elapsed_ms) > options_.preview_stale_timeout_ms) {
          error_kind = status_.frames_received == 0 ? "no_frames" : "frame_stalled";
          error_message = status_.frames_received == 0 ? "camera preview produced no frames"
                                                       : "camera preview frame stream stalled";
        }
      }
    }
  }
  if (error_kind.empty()) {
    return;
  }
  module_manager_.StopAll();
  SetError(std::move(error_kind), std::move(error_message));
}

CameraServiceStatus CameraService::status() const {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  CameraServiceStatus result = status_;
  result.modules = module_manager_.Status();
  return result;
}

bool CameraService::DeviceExists(const std::string& device, std::uint32_t width,
                                 std::uint32_t height, std::string* error) const {
  std::string list_error;
  const auto devices = ListDevices(&list_error);
  for (const auto& info : devices) {
    if (info.path == device && info.query_ok && info.supports_capture && info.supports_streaming) {
      if (options_.capture_pipeline == CameraCapturePipeline::kArgusIsp ||
          options_.capture_pipeline == CameraCapturePipeline::kSynthetic) {
        return true;
      }
      std::string format_error;
      const auto formats = format_lister_(device, &format_error);
      const std::uint32_t required_fourcc =
          options_.capture_pipeline == CameraCapturePipeline::kSoftwareIsp ? V4L2_PIX_FMT_SRGGB10
          : options_.uvc_input_format == CameraUvcInputFormat::kMjpeg      ? V4L2_PIX_FMT_MJPEG
                                                                           : V4L2_PIX_FMT_YUYV;
      const bool require_frame_size = options_.capture_pipeline == CameraCapturePipeline::kUvc;
      const bool format_supported =
          std::any_of(formats.begin(), formats.end(), [&](const PixelFormatInfo& format) {
            if (format.fourcc != required_fourcc) {
              return false;
            }
            return !require_frame_size || format.frame_sizes.empty() ||
                   std::any_of(format.frame_sizes.begin(), format.frame_sizes.end(),
                               [width, height](const FrameSizeInfo& size) {
                                 return size.width == width && size.height == height;
                               });
          });
      if (format_supported) {
        return true;
      }
      AssignError(error,
                  format_error.empty()
                      ? "camera device does not support the configured pipeline format and frame "
                        "size: " +
                            device
                      : format_error);
      return false;
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
  if (!frame.IsValid()) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++status_.frames_dropped;
    ++status_.consecutive_frame_drops;
    status_.max_consecutive_frame_drops =
        std::max(status_.max_consecutive_frame_drops, status_.consecutive_frame_drops);
    PublishStatusEvent(status_);
    return;
  }

  const std::uint64_t sequence = frame.sequence;
  const std::uint64_t timestamp_ms = frame.timestamp_ms;
  const std::uint64_t received_at_ms =
      static_cast<std::uint64_t>(time::WallTime::Now().ToMilliseconds());
  PublishFrameEvent(frame, received_at_ms);
  if (frame_sink_ == nullptr || !frame_sink_->Publish(std::move(frame))) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++status_.frames_dropped;
    ++status_.consecutive_frame_drops;
    status_.max_consecutive_frame_drops =
        std::max(status_.max_consecutive_frame_drops, status_.consecutive_frame_drops);
    PublishStatusEvent(status_);
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.frames_received > 0 && sequence > status_.last_frame_sequence + 1U) {
    status_.source_frames_skipped += sequence - status_.last_frame_sequence - 1U;
    ++status_.consecutive_source_gaps;
    status_.max_consecutive_source_gaps =
        std::max(status_.max_consecutive_source_gaps, status_.consecutive_source_gaps);
  } else {
    status_.consecutive_source_gaps = 0;
  }
  ++status_.frames_received;
  status_.consecutive_frame_drops = 0;
  status_.last_frame_sequence = sequence;
  status_.last_frame_timestamp_ms = timestamp_ms;
  status_.last_frame_received_at_ms = received_at_ms;
  last_frame_received_steady_ = std::chrono::steady_clock::now();
}

void CameraService::SetError(std::string kind, std::string error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = CameraPreviewState::kFaulted;
  status_.last_error_kind = std::move(kind);
  status_.last_error = std::move(error);
  PublishStatusEvent(status_);
}

void CameraService::PublishStatusEvent(const CameraServiceStatus& status) const {
  if (message_bus_ == nullptr) {
    return;
  }
  std::ostringstream payload;
  payload << "{"
          << "\"state\":" << static_cast<int>(status.state) << ',' << "\"device\":\""
          << json::EscapeString(status.device) << "\","
          << "\"width\":" << status.width << ',' << "\"height\":" << status.height << ','
          << "\"fps\":" << status.fps << ',' << "\"frames_received\":" << status.frames_received
          << ',' << "\"frames_dropped\":" << status.frames_dropped << ','
          << "\"source_frames_skipped\":" << status.source_frames_skipped << ','
          << "\"last_frame_sequence\":" << status.last_frame_sequence << ','
          << "\"last_error_kind\":\"" << json::EscapeString(status.last_error_kind) << "\","
          << "\"restart_count\":" << status.restart_count << ','
          << "\"recover_count\":" << status.recover_count << '}';
  message_bus_->Publish(event::EventMessage{"/camera/status", "camera.status", "camera-service",
                                            payload.str(), time::WallTime::Now().ToMilliseconds(),
                                            0});
}

void CameraService::PublishFrameEvent(const CameraFrame& frame,
                                      std::uint64_t received_at_ms) const {
  if (message_bus_ == nullptr) {
    return;
  }
  std::ostringstream payload;
  payload << "{"
          << "\"sequence\":" << frame.sequence << ','
          << "\"frame_timestamp_ms\":" << frame.timestamp_ms << ','
          << "\"source_timestamp_ns\":" << frame.source_timestamp_ns << ','
          << "\"source_timestamp_valid\":" << (frame.source_timestamp_valid ? "true" : "false")
          << ',' << "\"source_clock\":\"" << ToString(frame.source_clock) << "\","
          << "\"source_timestamp_flags\":" << frame.source_timestamp_flags << ','
          << "\"frame_received_at_ns\":" << frame.received_at_ns << ','
          << "\"received_at_ms\":" << received_at_ms << ',' << "\"width\":" << frame.width << ','
          << "\"height\":" << frame.height << ',' << "\"stride_bytes\":" << frame.stride_bytes
          << ',' << "\"size_bytes\":" << frame.data.size() << '}';
  message_bus_->Publish(event::EventMessage{"/camera/frame_meta", "camera.frame_meta",
                                            "camera-service", payload.str(),
                                            static_cast<std::int64_t>(received_at_ms), 0});
}

}  // namespace camera
}  // namespace cockpit
