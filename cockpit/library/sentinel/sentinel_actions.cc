#include "cockpit/library/sentinel/sentinel_actions.h"

#include <chrono>
#include <sstream>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace sentinel {
namespace {

std::shared_ptr<grpc::Channel> CreateChannel(const std::string& address) {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  arguments.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 100);
  arguments.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 500);
  return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments);
}

}  // namespace

GrpcSentinelActions::GrpcSentinelActions(const std::string& camera_address,
                                         const std::string& recording_address, int rpc_timeout_ms)
    : camera_(proto::camera::CameraControl::NewStub(CreateChannel(camera_address))),
      recording_(proto::recording::RecordingControl::NewStub(CreateChannel(recording_address))),
      rpc_timeout_ms_(rpc_timeout_ms) {
}

bool GrpcSentinelActions::PrepareRecording(std::uint64_t event_sequence, std::string* error) {
  if (error == nullptr) return false;
  error->clear();
  proto::common::Empty empty;
  proto::recording::RecordingStatus current;
  grpc::ClientContext status_context;
  status_context.set_wait_for_ready(true);
  status_context.set_deadline(std::chrono::system_clock::now() +
                              std::chrono::milliseconds(rpc_timeout_ms_));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_context_ = &status_context;
  }
  grpc::Status status = recording_->GetStatus(&status_context, empty, &current);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_context_ == &status_context) active_context_ = nullptr;
  }
  if (!status.ok()) {
    *error = "recording status failed: " + status.error_message();
    return false;
  }
  if (current.state() == proto::recording::RECORDING_STATE_RECORDING) return true;

  proto::recording::StartRecordingRequest request;
  request.set_trigger("sentinel_motion_" + std::to_string(event_sequence));
  proto::recording::RecordingStatus response;
  grpc::ClientContext start_context;
  start_context.set_deadline(std::chrono::system_clock::now() +
                             std::chrono::milliseconds(rpc_timeout_ms_));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_context_ = &start_context;
  }
  status = recording_->Start(&start_context, request, &response);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_context_ == &start_context) active_context_ = nullptr;
  }
  if (!status.ok()) {
    *error = "recording start failed: " + status.error_message();
    return false;
  }
  return true;
}

bool GrpcSentinelActions::TakeSnapshot(std::uint64_t event_sequence, SentinelSnapshot* snapshot,
                                       std::string* error) {
  if (snapshot == nullptr || error == nullptr) return false;
  error->clear();
  proto::camera::TakePhotoRequest request;
  request.set_filename("sentinel-motion-" + std::to_string(event_sequence) + ".jpg");
  proto::camera::TakePhotoResponse response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(rpc_timeout_ms_));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_context_ = &context;
  }
  const grpc::Status status = camera_->TakePhoto(&context, request, &response);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_context_ == &context) active_context_ = nullptr;
  }
  if (!status.ok()) {
    *error = "camera snapshot failed: " + status.error_message();
    return false;
  }
  snapshot->path = response.path();
  snapshot->size_bytes = response.size_bytes();
  return true;
}

bool GrpcSentinelActions::RecordMotion(const vehicle::ChassisEvent& event,
                                       const SentinelSnapshot& snapshot, std::string* error) {
  if (error == nullptr) return false;
  error->clear();
  std::ostringstream payload;
  payload << "{\"sequence\":" << event.sequence << ",\"sensor_id\":" << event.sensor_id
          << ",\"snapshot_path\":\"" << json::EscapeString(snapshot.path) << "\"}";
  proto::recording::AppendRecordingEventRequest request;
  request.set_timestamp_ms(event.timestamp_ms);
  request.set_topic("/sentinel/motion_detected");
  request.set_payload_json(payload.str());
  proto::recording::RecordingStatus response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(rpc_timeout_ms_));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_context_ = &context;
  }
  const grpc::Status status = recording_->AppendEvent(&context, request, &response);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_context_ == &context) active_context_ = nullptr;
  }
  if (!status.ok()) {
    *error = "recording event failed: " + status.error_message();
    return false;
  }
  return true;
}

void GrpcSentinelActions::Cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_context_ != nullptr) active_context_->TryCancel();
}

}  // namespace sentinel
}  // namespace cockpit
