#include "vehicle_grpc_service.h"

#include <algorithm>
#include <chrono>

#include "cockpit/core/logging/Logger.h"

namespace cockpit {
namespace vehicle {
namespace {

proto::vehicle::VehicleState ToProto(const VehicleState& state) {
  proto::vehicle::VehicleState message;
  message.set_timestamp_ms(state.timestamp_ms);
  message.set_speed_kph(state.speed_kph);
  message.set_gear(state.gear);
  message.set_soc_percent(state.soc_percent);
  message.set_cloud_enabled(state.cloud_enabled);
  message.set_source(state.source);
  return message;
}

}  // namespace

VehicleGrpcService::~VehicleGrpcService() {
  Shutdown();
}

bool VehicleGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("failed to start vehicle gRPC server address=" + address);
    return false;
  }
  LOG_INFO("vehicle gRPC server listening address=" + address);
  return true;
}

void VehicleGrpcService::Publish(const VehicleState& state) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_state_ = state;
    ++version_;
  }
  state_changed_.notify_all();
}

void VehicleGrpcService::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    stopping_ = true;
  }
  state_changed_.notify_all();
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status VehicleGrpcService::SubscribeVehicleState(
    grpc::ServerContext* context, const proto::vehicle::SubscribeVehicleStateRequest* request,
    grpc::ServerWriter<proto::vehicle::VehicleState>* writer) {
  const int requested_hz = request->max_hz() <= 0 ? 10 : request->max_hz();
  const int max_hz = std::clamp(requested_hz, 1, 100);
  const auto min_interval = std::chrono::milliseconds(1000 / max_hz);
  auto next_write = std::chrono::steady_clock::now();
  std::uint64_t observed_version = 0;

  LOG_INFO("vehicle state subscriber connected consumer=" + request->consumer());
  while (!context->IsCancelled()) {
    VehicleState state;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      state_changed_.wait_for(lock, std::chrono::milliseconds(100), [this, observed_version] {
        return stopping_ || version_ > observed_version;
      });
      if (stopping_) {
        break;
      }
      if (version_ <= observed_version) {
        continue;
      }
      if (std::chrono::steady_clock::now() < next_write) {
        state_changed_.wait_until(lock, next_write, [this] {
          return stopping_;
        });
        if (stopping_) {
          break;
        }
      }
      state = latest_state_;
      observed_version = version_;
    }

    if (!writer->Write(ToProto(state))) {
      break;
    }
    next_write = std::chrono::steady_clock::now() + min_interval;
  }
  LOG_INFO("vehicle state subscriber disconnected consumer=" + request->consumer());
  return grpc::Status::OK;
}

}  // namespace vehicle
}  // namespace cockpit
