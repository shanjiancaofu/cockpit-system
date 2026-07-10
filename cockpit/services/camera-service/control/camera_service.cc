#include "camera_service.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <utility>

#include "cockpit/core/json/json.h"
#include "cockpit/core/utils/Time.h"
#include "cockpit/modules/camera/frames/latest_frame_buffer.h"

#if defined(COCKPIT_HAS_GSTREAMER_CAMERA)
#include "cockpit/modules/camera/capture/gstreamer_preview_pipeline.h"
#endif

namespace cockpit {
namespace camera {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::unique_ptr<CameraPreviewSource> CreateDefaultPreviewSource() {
#if defined(COCKPIT_HAS_GSTREAMER_CAMERA)
  return std::make_unique<GstreamerPreviewPipeline>();
#else
  return nullptr;
#endif
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
    : CameraService(
          [](std::string* error) {
            return V4l2Camera::ListDevices(error);
          },
          CreateDefaultPreviewSource(), std::move(frame_sink), std::move(message_bus)) {
}

CameraService::CameraService(DeviceLister device_lister,
                             std::unique_ptr<CameraPreviewSource> preview_source,
                             std::shared_ptr<CameraFrameSink> frame_sink)
    : CameraService(std::move(device_lister), std::move(preview_source), std::move(frame_sink),
                    nullptr) {
}

CameraService::CameraService(DeviceLister device_lister,
                             std::unique_ptr<CameraPreviewSource> preview_source,
                             std::shared_ptr<CameraFrameSink> frame_sink,
                             std::shared_ptr<event::MessageBus> message_bus)
    : device_lister_(std::move(device_lister)),
      frame_sink_(frame_sink == nullptr ? std::make_shared<LatestFrameBuffer>()
                                        : std::move(frame_sink)),
      message_bus_(std::move(message_bus)) {
  auto preview_module = std::make_unique<CameraPreviewModule>(std::move(preview_source));
  preview_module_ = preview_module.get();
  module_manager_.Add(std::move(preview_module));
}

CameraService::~CameraService() {
  StopPreview();
}

std::vector<VideoDeviceInfo> CameraService::ListDevices(std::string* error) const {
  return device_lister_(error);
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
  if (!DeviceExists(request.device, error)) {
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
    status_.preview_started_at_ms = static_cast<std::uint64_t>(utils::NowMs());
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

CameraServiceStatus CameraService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  CameraServiceStatus result = status_;
  result.modules = module_manager_.Status();
  return result;
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
  const std::uint64_t received_at_ms = static_cast<std::uint64_t>(utils::NowMs());
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
                                            payload.str(), utils::NowMs(), 0});
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
          << "\"received_at_ms\":" << received_at_ms << ',' << "\"width\":" << frame.width << ','
          << "\"height\":" << frame.height << ',' << "\"stride_bytes\":" << frame.stride_bytes
          << ',' << "\"size_bytes\":" << frame.data.size() << '}';
  message_bus_->Publish(event::EventMessage{"/camera/frame_meta", "camera.frame_meta",
                                            "camera-service", payload.str(),
                                            static_cast<std::int64_t>(received_at_ms), 0});
}

}  // namespace camera
}  // namespace cockpit
