#include "gateway_grpc_service.h"

#include "core/logging/Logger.h"

#include <algorithm>
#include <chrono>

namespace cockpit {
namespace gateway {

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
    ++version_;
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

grpc::Status GatewayGrpcService::SubscribeCockpitEvents(
    grpc::ServerContext* context,
    const proto::gateway::SubscribeCockpitEventsRequest* request,
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
        event_changed_.wait_until(lock, next_write, [this] { return stopping_; });
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
