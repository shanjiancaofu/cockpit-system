#include "cockpit/library/sentinel/sentinel_grpc_service.h"

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

namespace cockpit {
namespace sentinel {
namespace {

proto::sentinel::SentinelState ToProto(SentinelState state) {
  switch (state) {
    case SentinelState::kDisabled:
      return proto::sentinel::SENTINEL_STATE_DISABLED;
    case SentinelState::kArmed:
      return proto::sentinel::SENTINEL_STATE_ARMED;
    case SentinelState::kTriggered:
      return proto::sentinel::SENTINEL_STATE_TRIGGERED;
    case SentinelState::kCooldown:
      return proto::sentinel::SENTINEL_STATE_COOLDOWN;
    case SentinelState::kFaulted:
      return proto::sentinel::SENTINEL_STATE_FAULTED;
  }
  return proto::sentinel::SENTINEL_STATE_UNSPECIFIED;
}

}  // namespace

SentinelGrpcService::SentinelGrpcService(SentinelService& service) : service_(service) {
}
SentinelGrpcService::~SentinelGrpcService() {
  Shutdown();
}

bool SentinelGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (server_ == nullptr) {
    LOG_ERROR("failed to start sentinel gRPC server address=" + address);
    return false;
  }
  LOG_INFO("sentinel gRPC server listening address=" + address);
  return true;
}

void SentinelGrpcService::Shutdown() {
  if (server_ != nullptr) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status SentinelGrpcService::Arm(grpc::ServerContext*, const proto::common::Empty*,
                                      proto::sentinel::SentinelStatus* response) {
  std::string error;
  if (!service_.Arm(&error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status SentinelGrpcService::Disarm(grpc::ServerContext*, const proto::common::Empty*,
                                         proto::sentinel::SentinelStatus* response) {
  std::string error;
  if (!service_.Disarm(&error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status SentinelGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                            proto::sentinel::SentinelStatus* response) {
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

void SentinelGrpcService::FillStatus(const SentinelStatus& status,
                                     proto::sentinel::SentinelStatus* response) {
  response->set_state(ToProto(status.state));
  response->set_cooldown_until_ms(status.cooldown_until_ms);
  response->set_last_event_sequence(status.last_event_sequence);
  response->set_last_event_timestamp_ms(status.last_event_timestamp_ms);
  response->set_last_snapshot_path(status.last_snapshot_path);
  response->set_accepted_events(status.accepted_events);
  response->set_suppressed_events(status.suppressed_events);
  response->set_failed_events(status.failed_events);
  response->set_dropped_events(status.dropped_events);
  response->set_last_error(status.last_error);
  auto* health = response->mutable_health();
  health->set_service_name("sentinel-service");
  health->set_checked_at_ms(time::WallTime::Now().ToMilliseconds());
  health->set_last_error(status.last_error);
  if (status.state == SentinelState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "sentinel faulted" : status.last_error);
  } else if (!status.last_error.empty()) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message(status.last_error);
  } else if (status.state == SentinelState::kDisabled) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DISABLED);
    health->set_message("sentinel disarmed");
  } else {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
    health->set_message("sentinel " + std::string(SentinelStateName(status.state)));
  }
}

}  // namespace sentinel
}  // namespace cockpit
