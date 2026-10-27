#include "cockpit/driver/camera/runtime/grpc/camera_grpc_service.h"

#include <sstream>

#include "cockpit/exchange/foundation/json/json.h"
#include "cockpit/exchange/foundation/logging/logger.h"
#include "cockpit/exchange/foundation/time/time.h"

namespace cockpit {
namespace camera {
namespace {

proto::camera::CameraPreviewState ToProtoState(CameraPreviewState state) {
  switch (state) {
    case CameraPreviewState::kStopped:
      return proto::camera::CAMERA_PREVIEW_STATE_STOPPED;
    case CameraPreviewState::kRunning:
      return proto::camera::CAMERA_PREVIEW_STATE_RUNNING;
    case CameraPreviewState::kRecovering:
      return proto::camera::CAMERA_PREVIEW_STATE_RECOVERING;
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
  health->set_checked_at_ms(time::WallTime::Now().ToMilliseconds());
  health->set_last_error(status.last_error);
  if (status.state == CameraPreviewState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "camera preview faulted" : status.last_error);
    return;
  }
  if (status.state == CameraPreviewState::kRecovering) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message("camera preview recovering");
    return;
  }
  if (status.state == CameraPreviewState::kStopped) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DISABLED);
    health->set_message("camera preview stopped");
    return;
  }
  health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
  health->set_message("camera service online");
}

std::string PhotoEventPayload(const CameraPhotoResult& result) {
  std::ostringstream output;
  output << "{"
         << "\"path\":\"" << json::EscapeString(result.path) << "\","
         << "\"frame_sequence\":" << result.frame_sequence << ','
         << "\"frame_timestamp_ms\":" << result.frame_timestamp_ms << ','
         << "\"width\":" << result.width << ',' << "\"height\":" << result.height << ','
         << "\"size_bytes\":" << result.size_bytes << "}";
  return output.str();
}

}  // namespace

CameraGrpcService::CameraGrpcService(CameraService& camera_service,
                                     CameraPhotoService& photo_service,
                                     recording::RecordingEventPublisher* recording_events)
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
  start_request.h264_output_path = request->h264_output_path();
  start_request.h264_width = request->h264_width() == 0 ? 1280U : request->h264_width();
  start_request.h264_height = request->h264_height() == 0 ? 720U : request->h264_height();
  start_request.h264_bitrate = request->h264_bitrate() == 0 ? 2'000'000U : request->h264_bitrate();
  start_request.h264_gop = request->h264_gop() == 0 ? 30U : request->h264_gop();

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
    const auto timestamp_ms = static_cast<std::int64_t>(time::WallTime::Now().ToMilliseconds());
    proto::recording::AppendRecordingDataFileRequest data_file;
    data_file.set_timestamp_ms(timestamp_ms);
    data_file.set_source("camera");
    data_file.set_kind("jpeg");
    data_file.set_path(result.path);
    data_file.set_size_bytes(result.size_bytes);
    data_file.set_copy_into_session(true);
    recording_events_->PublishDataFile(std::move(data_file));
    recording_events_->Publish(timestamp_ms, "/camera/photo", PhotoEventPayload(result));
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
  response->set_preview_started_at_ms(status.preview_started_at_ms);
  response->set_consecutive_frame_drops(status.consecutive_frame_drops);
  response->set_max_consecutive_frame_drops(status.max_consecutive_frame_drops);
  response->set_consecutive_source_gaps(status.consecutive_source_gaps);
  response->set_max_consecutive_source_gaps(status.max_consecutive_source_gaps);
  response->set_last_error_kind(status.last_error_kind);
  response->set_restart_count(status.restart_count);
  response->set_recover_count(status.recover_count);
  response->set_last_recover_at_ms(status.last_recover_at_ms);
  response->set_native_metrics_valid(status.native_metrics_valid);
  response->set_native_frames_delivered(status.native_frames_delivered);
  response->set_native_delivery_p95_ms(status.native_delivery_p95_ms);
  response->set_native_capture_frames_acquired(status.native_capture_frames_acquired);
  response->set_native_capture_leases_returned(status.native_capture_leases_returned);
  response->set_native_capture_buffers_released(status.native_capture_buffers_released);
  response->set_native_broker_budget_dropped(status.native_broker_budget_dropped);
  response->set_native_broker_retained_frames(status.native_broker_retained_frames);
  response->set_native_vic_transformed(status.native_vic_transformed);
  response->set_native_vic_total_ms(status.native_vic_total_ms);
  response->set_native_encoder_frames_submitted(status.native_encoder_frames_submitted);
  response->set_native_encoder_packets(status.native_encoder_packets);
  response->set_native_encoder_keyframes(status.native_encoder_keyframes);
  response->set_native_encoder_bytes(status.native_encoder_bytes);
  response->set_last_error(status.last_error);
  FillHealth(status, response->mutable_health());
  for (const auto& module : status.modules) {
    auto* module_status = response->add_modules();
    module_status->set_name(module.name);
    module_status->set_state(ToProtoModuleState(module.state));
  }
}

grpc::Status CameraGrpcService::FillTuningResponse(const CameraServiceStatus& service_status,
                                                   const CameraTuningState* state, bool accepted,
                                                   const std::string& error,
                                                   proto::camera::CameraTuningResponse* response) {
  response->Clear();
  response->set_accepted(accepted);
  response->set_error(error);
  FillStatus(service_status, response->mutable_status());
  if (state != nullptr) {
    response->set_session_id(state->session_id);
    response->set_revision(state->revision);
    response->set_applied(state->last_observed.valid &&
                          state->last_observed.revision == state->revision);
    auto* observed = response->mutable_last_observed();
    observed->set_valid(state->last_observed.valid);
    observed->set_revision(state->last_observed.revision);
    observed->set_exposure_ns(state->last_observed.exposure_ns);
    observed->set_sensor_gain(state->last_observed.sensor_gain);
    observed->set_isp_gain(state->last_observed.isp_gain);
    for (const auto gain : state->last_observed.wb_gains) observed->add_wb_gains(gain);
    observed->set_ae_mode(state->last_observed.ae_mode);
    observed->set_awb_mode(state->last_observed.awb_mode);
    observed->set_ae_locked(state->last_observed.ae_locked);
    observed->set_awb_locked(state->last_observed.awb_locked);
    observed->set_awb_cct(state->last_observed.awb_cct);
    TuningProfile profile = MakeTuningProfile(*state, "camera-service-session");
    std::string yaml;
    std::string encode_error;
    if (EncodeTuningProfile(profile, &yaml, &encode_error)) response->set_profile_yaml(yaml);
  }
  return accepted ? grpc::Status::OK
                  : grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                 error.empty() ? "camera tuning request rejected" : error);
}

grpc::Status CameraGrpcService::GetTuning(grpc::ServerContext*, const proto::common::Empty*,
                                          proto::camera::CameraTuningResponse* response) {
  CameraTuningState state;
  std::string error;
  const bool ok = camera_service_.GetNativeTuning(&state, &error);
  return FillTuningResponse(camera_service_.status(), ok ? &state : nullptr, ok, error, response);
}

grpc::Status CameraGrpcService::SetTuning(grpc::ServerContext*,
                                          const proto::camera::CameraTuningRequest* request,
                                          proto::camera::CameraTuningResponse* response) {
  TuningProfile profile;
  std::string error;
  if (!DecodeTuningProfile(request->profile_yaml(), &profile, &error))
    return FillTuningResponse(camera_service_.status(), nullptr, false, error, response);
  CameraTuningState state;
  const bool ok = camera_service_.SetNativeTuning(request->session_id(), request->revision(),
                                                  profile.settings, &state, &error);
  return FillTuningResponse(camera_service_.status(), ok ? &state : nullptr, ok, error, response);
}

grpc::Status CameraGrpcService::ResetTuning(grpc::ServerContext*,
                                            const proto::camera::CameraTuningRequest* request,
                                            proto::camera::CameraTuningResponse* response) {
  CameraTuningState state;
  std::string error;
  const bool ok =
      camera_service_.ResetNativeTuning(request->session_id(), request->revision(), &state, &error);
  return FillTuningResponse(camera_service_.status(), ok ? &state : nullptr, ok, error, response);
}

grpc::Status CameraGrpcService::ApplyTuningProfile(
    grpc::ServerContext*, const proto::camera::CameraTuningRequest* request,
    proto::camera::CameraTuningResponse* response) {
  TuningProfile profile;
  std::string error;
  if (!DecodeTuningProfile(request->profile_yaml(), &profile, &error))
    return FillTuningResponse(camera_service_.status(), nullptr, false, error, response);
  CameraTuningState state;
  const bool ok = camera_service_.ApplyNativeTuningProfile(
      request->session_id(), request->revision(), profile, &state, &error);
  return FillTuningResponse(camera_service_.status(), ok ? &state : nullptr, ok, error, response);
}

}  // namespace camera
}  // namespace cockpit
