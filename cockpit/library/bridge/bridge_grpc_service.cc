#include "cockpit/library/bridge/bridge_grpc_service.h"

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

namespace cockpit::bridge {
namespace {

proto::bridge::BridgeState ToProto(BridgeState state) {
  switch (state) {
    case BridgeState::kDisabled:
      return proto::bridge::BRIDGE_STATE_DISABLED;
    case BridgeState::kIdle:
      return proto::bridge::BRIDGE_STATE_IDLE;
    case BridgeState::kAccepted:
      return proto::bridge::BRIDGE_STATE_ACCEPTED;
    case BridgeState::kExecuting:
      return proto::bridge::BRIDGE_STATE_EXECUTING;
    case BridgeState::kSucceeded:
      return proto::bridge::BRIDGE_STATE_SUCCEEDED;
    case BridgeState::kCancelled:
      return proto::bridge::BRIDGE_STATE_CANCELLED;
    case BridgeState::kRejected:
      return proto::bridge::BRIDGE_STATE_REJECTED;
    case BridgeState::kFailed:
      return proto::bridge::BRIDGE_STATE_FAILED;
    case BridgeState::kTimedOut:
      return proto::bridge::BRIDGE_STATE_TIMED_OUT;
    case BridgeState::kDisconnected:
      return proto::bridge::BRIDGE_STATE_DISCONNECTED;
  }
  return proto::bridge::BRIDGE_STATE_UNSPECIFIED;
}

void FillPose(const BridgePose& pose, proto::bridge::BridgePose* response) {
  response->set_x_m(pose.x_m);
  response->set_y_m(pose.y_m);
  response->set_yaw_rad(pose.yaw_rad);
  response->set_frame_id(pose.frame_id);
  response->set_timestamp_ms(pose.timestamp_ms);
}

}  // namespace

BridgeGrpcService::BridgeGrpcService(BridgeService& service) : service_(service) {
}
BridgeGrpcService::~BridgeGrpcService() {
  Shutdown();
}

bool BridgeGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (server_ == nullptr) {
    LOG_ERROR("failed to start bridge gRPC server address=" + address);
    return false;
  }
  LOG_INFO("bridge gRPC server listening address=" + address);
  return true;
}

void BridgeGrpcService::Shutdown() {
  if (server_ != nullptr) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status BridgeGrpcService::SubmitGoal(grpc::ServerContext*,
                                           const proto::bridge::SubmitBridgeGoalRequest* request,
                                           proto::bridge::BridgeStatus* response) {
  BridgeGoal goal;
  goal.goal_id = request->goal_id();
  goal.target.x_m = request->target().x_m();
  goal.target.y_m = request->target().y_m();
  goal.target.yaw_rad = request->target().yaw_rad();
  goal.target.frame_id = request->target().frame_id();
  goal.target.timestamp_ms = request->target().timestamp_ms();
  BridgeStatus status;
  std::string error;
  if (!service_.SubmitGoal(goal, &status, &error)) {
    FillStatus(status, response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(status, response);
  return grpc::Status::OK;
}

grpc::Status BridgeGrpcService::CancelGoal(grpc::ServerContext*,
                                           const proto::bridge::CancelBridgeGoalRequest* request,
                                           proto::bridge::BridgeStatus* response) {
  BridgeStatus status;
  std::string error;
  if (!service_.CancelGoal(request->goal_id(), &status, &error)) {
    FillStatus(status, response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(status, response);
  return grpc::Status::OK;
}

grpc::Status BridgeGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                          proto::bridge::BridgeStatus* response) {
  FillStatus(service_.GetStatus(), response);
  return grpc::Status::OK;
}

void BridgeGrpcService::FillStatus(const BridgeStatus& status,
                                   proto::bridge::BridgeStatus* response) {
  response->set_state(ToProto(status.state));
  response->set_goal_id(status.goal_id);
  FillPose(status.target, response->mutable_target());
  FillPose(status.current_pose, response->mutable_current_pose());
  response->set_accepted_at_ms(status.accepted_at_ms);
  response->set_updated_at_ms(status.updated_at_ms);
  response->set_message(status.message);
  response->set_last_error(status.last_error);
  auto* health = response->mutable_health();
  health->set_service_name("bridge-service");
  health->set_checked_at_ms(time::NowMs());
  health->set_last_error(status.last_error);
  if (status.state == BridgeState::kDisabled) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DISABLED);
  } else if (status.state == BridgeState::kDisconnected) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
  } else if (status.state == BridgeState::kFailed || status.state == BridgeState::kTimedOut) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
  } else {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
  }
  health->set_message(status.message.empty() ? BridgeStateName(status.state) : status.message);
}

}  // namespace cockpit::bridge
