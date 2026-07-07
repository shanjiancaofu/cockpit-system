#include "camera_grpc_service.h"

#include <sstream>

#include "cockpit/core/logging/Logger.h"
#include "cockpit/core/utils/Time.h"

namespace cockpit {
namespace camera {
namespace {

proto::camera::CameraPreviewState ToProtoState(CameraPreviewState state) {
  switch (state) {
    case CameraPreviewState::kStopped:
      return proto::camera::CAMERA_PREVIEW_STATE_STOPPED;
    case CameraPreviewState::kRunning:
      return proto::camera::CAMERA_PREVIEW_STATE_RUNNING;
    case CameraPreviewState::kFaulted:
      return proto::camera::CAMERA_PREVIEW_STATE_FAULTED;
  }
  return proto::camera::CAMERA_PREVIEW_STATE_UNSPECIFIED;
}

proto::common::RuntimeModuleState ToProtoModuleState(runtime::ModuleState state) {
  switch (state) {
    case runtime::ModuleState::kCreated:
      return proto::common::RUNTIME_MODULE_STATE_CREATED;
    case runtime::ModuleState::kStarting:
      return proto::common::RUNTIME_MODULE_STATE_STARTING;
    case runtime::ModuleState::kRunning:
      return proto::common::RUNTIME_MODULE_STATE_RUNNING;
    case runtime::ModuleState::kStopping:
      return proto::common::RUNTIME_MODULE_STATE_STOPPING;
    case runtime::ModuleState::kStopped:
      return proto::common::RUNTIME_MODULE_STATE_STOPPED;
    case runtime::ModuleState::kFailed:
      return proto::common::RUNTIME_MODULE_STATE_FAILED;
  }
  return proto::common::RUNTIME_MODULE_STATE_UNSPECIFIED;
}

void FillHealth(const CameraServiceStatus& status, proto::common::ServiceHealth* health) {
  health->set_service_name("camera-service");
  health->set_checked_at_ms(utils::NowMs());
  health->set_last_error(status.last_error);
  if (status.state == CameraPreviewState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "camera preview faulted" : status.last_error);
    return;
  }
  if (status.state == CameraPreviewState::kStopped) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message("camera preview stopped");
    return;
  }
  health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
  health->set_message("camera service online");
}

std::string EscapeJson(const std::string& input) {
  std::ostringstream output;
  for (const char character : input) {
    switch (character) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      default:
        output << character;
        break;
    }
  }
  return output.str();
}

std::string PhotoEventPayload(const CameraPhotoResult& result) {
  std::ostringstream output;
  output << "{"
         << "\"path\":\"" << EscapeJson(result.path) << "\","
         << "\"frame_sequence\":" << result.frame_sequence << ','
         << "\"frame_timestamp_ms\":" << result.frame_timestamp_ms << ','
         << "\"width\":" << result.width << ',' << "\"height\":" << result.height << ','
         << "\"size_bytes\":" << result.size_bytes << "}";
  return output.str();
}

}  // namespace

CameraGrpcService::CameraGrpcService(CameraService& camera_service,
                                     CameraPhotoService& photo_service,
                                     const recording::RecordingEventPublisher* recording_events)
    : camera_service_(camera_service),
      photo_service_(photo_service),
      recording_events_(recording_events) {
}

CameraGrpcService::~CameraGrpcService() {
  Shutdown();
}

bool CameraGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("failed to start camera gRPC server address=" + address);
    return false;
  }
  LOG_INFO("camera gRPC server listening address=" + address);
  return true;
}

void CameraGrpcService::Shutdown() {
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status CameraGrpcService::ListDevices(grpc::ServerContext*, const proto::common::Empty*,
                                            proto::camera::ListCameraDevicesResponse* response) {
  std::string error;
  const auto devices = camera_service_.ListDevices(&error);
  if (!error.empty() && devices.empty()) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, error);
  }
  for (const auto& device : devices) {
    FillDevice(device, response->add_devices());
  }
  return grpc::Status::OK;
}

grpc::Status CameraGrpcService::StartPreview(grpc::ServerContext*,
                                             const proto::camera::StartPreviewRequest* request,
                                             proto::camera::CameraStatus* response) {
  CameraStartPreviewRequest start_request;
  start_request.device = request->device().empty() ? "/dev/video0" : request->device();
  start_request.width = request->width() == 0 ? 640U : request->width();
  start_request.height = request->height() == 0 ? 480U : request->height();
  start_request.fps = request->fps() == 0 ? 30U : request->fps();

  std::string error;
  if (!camera_service_.StartPreview(start_request, &error)) {
    FillStatus(camera_service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(camera_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status CameraGrpcService::StopPreview(grpc::ServerContext*, const proto::common::Empty*,
                                            proto::camera::CameraStatus* response) {
  camera_service_.StopPreview();
  FillStatus(camera_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status CameraGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                          proto::camera::CameraStatus* response) {
  FillStatus(camera_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status CameraGrpcService::TakePhoto(grpc::ServerContext*,
                                          const proto::camera::TakePhotoRequest* request,
                                          proto::camera::TakePhotoResponse* response) {
  CameraPhotoResult result;
  std::string error;
  if (!photo_service_.TakePhoto(request->filename(), &result, &error)) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  response->set_path(result.path);
  response->set_frame_sequence(result.frame_sequence);
  response->set_frame_timestamp_ms(result.frame_timestamp_ms);
  response->set_width(result.width);
  response->set_height(result.height);
  response->set_size_bytes(result.size_bytes);
  if (recording_events_ != nullptr) {
    recording_events_->Publish(static_cast<std::int64_t>(utils::NowMs()), "/camera/photo",
                               PhotoEventPayload(result));
  }
  return grpc::Status::OK;
}

void CameraGrpcService::FillDevice(const VideoDeviceInfo& device,
                                   proto::camera::CameraDevice* response) {
  response->set_path(device.path);
  response->set_driver(device.driver);
  response->set_card(device.card);
  response->set_bus_info(device.bus_info);
  response->set_query_ok(device.query_ok);
  response->set_supports_capture(device.supports_capture);
  response->set_supports_streaming(device.supports_streaming);
  response->set_error(device.error);
}

void CameraGrpcService::FillStatus(const CameraServiceStatus& status,
                                   proto::camera::CameraStatus* response) {
  response->set_state(ToProtoState(status.state));
  response->set_device(status.device);
  response->set_width(status.width);
  response->set_height(status.height);
  response->set_fps(status.fps);
  response->set_frames_received(status.frames_received);
  response->set_frames_dropped(status.frames_dropped);
  response->set_source_frames_skipped(status.source_frames_skipped);
  response->set_last_frame_sequence(status.last_frame_sequence);
  response->set_last_frame_timestamp_ms(status.last_frame_timestamp_ms);
  response->set_last_frame_received_at_ms(status.last_frame_received_at_ms);
  response->set_last_error(status.last_error);
  FillHealth(status, response->mutable_health());
  for (const auto& module : status.modules) {
    auto* module_status = response->add_modules();
    module_status->set_name(module.name);
    module_status->set_state(ToProtoModuleState(module.state));
  }
}

}  // namespace camera
}  // namespace cockpit
