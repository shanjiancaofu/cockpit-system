#include "gateway_grpc_service.h"

#include <algorithm>
#include <chrono>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/utils/time.h"

namespace cockpit {
namespace gateway {
namespace {

constexpr auto kVehicleStateFreshTimeout = std::chrono::seconds(2);

proto::gateway::TopicMetadata VehicleStateMetadata() {
  proto::gateway::TopicMetadata metadata;
  metadata.set_name("/vehicle/state");
  metadata.set_message_type("cockpit.proto.vehicle.VehicleState");
  metadata.set_source("vehicle-data-service");
  metadata.set_subscribable(true);
  metadata.set_publishable(false);
  return metadata;
}

}  // namespace

GatewayGrpcService::~GatewayGrpcService() {
  Shutdown();
}

bool GatewayGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("failed to start gateway gRPC server address=" + address);
    return false;
  }
  LOG_INFO("gateway gRPC server listening address=" + address);
  return true;
}

void GatewayGrpcService::PublishVehicleState(const proto::vehicle::VehicleState& state) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_event_.set_timestamp_ms(state.timestamp_ms());
    *latest_event_.mutable_vehicle_state() = state;
    latest_vehicle_update_ = std::chrono::steady_clock::now();
    ++version_;
    ++events_published_;
  }
  event_changed_.notify_all();
}

void GatewayGrpcService::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    stopping_ = true;
  }
  event_changed_.notify_all();
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status GatewayGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                           proto::gateway::GatewayStatus* response) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool has_vehicle_state = latest_event_.has_vehicle_state();
  const auto now = std::chrono::steady_clock::now();
  const auto age = has_vehicle_state ? now - latest_vehicle_update_ : std::chrono::seconds(0);
  const bool fresh = has_vehicle_state && age <= kVehicleStateFreshTimeout;
  auto* health = response->mutable_health();
  health->set_service_name("cockpit-gateway-service");
  health->set_checked_at_ms(utils::NowMs());
  if (!has_vehicle_state) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message("vehicle state is not available yet");
  } else if (!fresh) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message("vehicle state is stale");
  } else {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
    health->set_message("gateway online");
  }
  response->set_vehicle_state_available(fresh);
  response->set_last_vehicle_state_age_ms(
      has_vehicle_state ? std::chrono::duration_cast<std::chrono::milliseconds>(age).count() : -1);
  response->set_events_published(events_published_);
  return grpc::Status::OK;
}

grpc::Status GatewayGrpcService::GetLatestVehicleState(grpc::ServerContext*,
                                                       const proto::common::Empty*,
                                                       proto::vehicle::VehicleState* response) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!latest_event_.has_vehicle_state()) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "vehicle state is not available yet");
  }
  if (std::chrono::steady_clock::now() - latest_vehicle_update_ > kVehicleStateFreshTimeout) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "vehicle state is stale");
  }
  *response = latest_event_.vehicle_state();
  return grpc::Status::OK;
}

grpc::Status GatewayGrpcService::ListTopics(grpc::ServerContext*,
                                            const proto::gateway::ListTopicsRequest* request,
                                            proto::gateway::ListTopicsResponse* response) {
  *response->add_topics() = VehicleStateMetadata();
  LOG_DEBUG("topic list requested client_id=" + request->client_id());
  return grpc::Status::OK;
}

grpc::Status GatewayGrpcService::GetTopicInfo(grpc::ServerContext*,
                                              const proto::gateway::GetTopicInfoRequest* request,
                                              proto::gateway::TopicMetadata* response) {
  if (request->topic() != "/vehicle/state") {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "topic is not exposed by cockpit gateway");
  }
  *response = VehicleStateMetadata();
  LOG_DEBUG("topic info requested client_id=" + request->client_id() +
            " topic=" + request->topic());
  return grpc::Status::OK;
}

grpc::Status GatewayGrpcService::SubscribeCockpitEvents(
    grpc::ServerContext* context, const proto::gateway::SubscribeCockpitEventsRequest* request,
    grpc::ServerWriter<proto::gateway::CockpitEvent>* writer) {
  const int requested_hz = request->max_hz() <= 0 ? 10 : request->max_hz();
  const int max_hz = std::clamp(requested_hz, 1, 100);
  const auto min_interval = std::chrono::milliseconds(1000 / max_hz);
  auto next_write = std::chrono::steady_clock::now();
  std::uint64_t observed_version = 0;

  LOG_INFO("gateway subscriber connected client_id=" + request->client_id());
  while (!context->IsCancelled()) {
    proto::gateway::CockpitEvent event;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      event_changed_.wait_for(lock, std::chrono::milliseconds(100), [this, observed_version] {
        return stopping_ || version_ > observed_version;
      });
      if (stopping_) {
        break;
      }
      if (version_ <= observed_version) {
        continue;
      }
      if (std::chrono::steady_clock::now() < next_write) {
        event_changed_.wait_until(lock, next_write, [this] {
          return stopping_;
        });
        if (stopping_) {
          break;
        }
      }
      event = latest_event_;
      observed_version = version_;
    }

    if (!writer->Write(event)) {
      break;
    }
    next_write = std::chrono::steady_clock::now() + min_interval;
  }
  LOG_INFO("gateway subscriber disconnected client_id=" + request->client_id());
  return grpc::Status::OK;
}

}  // namespace gateway
}  // namespace cockpit
