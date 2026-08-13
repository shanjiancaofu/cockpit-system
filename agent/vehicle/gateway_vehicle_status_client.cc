#include "agent/vehicle/gateway_vehicle_status_client.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "common.pb.h"

namespace cockpit {
namespace voice {
namespace {

constexpr int kMaxAttempts = 5;
constexpr auto kRpcDeadline = std::chrono::milliseconds(500);
constexpr auto kRetryDelay = std::chrono::milliseconds(200);

}  // namespace

GatewayVehicleStatusClient::GatewayVehicleStatusClient(const std::string& address)
    : stub_([&address] {
        grpc::ChannelArguments arguments;
        arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        return proto::gateway::CockpitGateway::NewStub(
            grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments));
      }()) {
}

bool GatewayVehicleStatusClient::GetLatest(std::chrono::steady_clock::time_point deadline,
                                           VehicleStatusSnapshot* status, std::string* error) {
  const std::uint64_t generation = cancellation_generation_.load();
  std::string last_error;
  for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      if (error != nullptr) {
        *error = "Vehicle status request deadline exceeded.";
      }
      return false;
    }
    proto::common::Empty request;
    proto::vehicle::VehicleState response;
    grpc::ClientContext context;
    context.set_wait_for_ready(true);
    const auto rpc_budget = std::min(
        remaining, std::chrono::duration_cast<std::chrono::steady_clock::duration>(kRpcDeadline));
    context.set_deadline(std::chrono::system_clock::now() + rpc_budget);
    {
      std::lock_guard<std::mutex> lock(cancellation_mutex_);
      if (cancellation_generation_.load() != generation) {
        if (error != nullptr) {
          *error = "Vehicle status request cancelled.";
        }
        return false;
      }
      active_context_ = &context;
    }
    const grpc::Status rpc_status = stub_->GetLatestVehicleState(&context, request, &response);
    {
      std::lock_guard<std::mutex> lock(cancellation_mutex_);
      if (active_context_ == &context) {
        active_context_ = nullptr;
      }
    }
    if (cancellation_generation_.load() != generation) {
      if (error != nullptr) {
        *error = "Vehicle status request cancelled.";
      }
      return false;
    }
    if (rpc_status.ok()) {
      if (status != nullptr) {
        status->timestamp_ms = response.timestamp_ms();
        status->speed_kph = response.speed_kph();
        status->gear = response.gear();
        status->soc_percent = response.soc_percent();
        status->source = response.source();
      }
      return true;
    }

    last_error = rpc_status.error_message();
    if (attempt < kMaxAttempts && !WaitForRetry(generation, deadline)) {
      if (error != nullptr) {
        *error = cancellation_generation_.load() != generation
                     ? "Vehicle status request cancelled."
                     : "Vehicle status request deadline exceeded.";
      }
      return false;
    }
  }

  if (error != nullptr) {
    *error = "Vehicle status unavailable: " + last_error;
  }
  return false;
}

void GatewayVehicleStatusClient::Cancel() {
  cancellation_generation_.fetch_add(1U);
  {
    std::lock_guard<std::mutex> lock(cancellation_mutex_);
    if (active_context_ != nullptr) {
      active_context_->TryCancel();
    }
  }
  cancellation_changed_.notify_all();
}

bool GatewayVehicleStatusClient::WaitForRetry(std::uint64_t generation,
                                              std::chrono::steady_clock::time_point deadline) {
  const auto remaining = deadline - std::chrono::steady_clock::now();
  if (remaining <= std::chrono::steady_clock::duration::zero()) {
    return false;
  }
  const auto delay = std::min(
      remaining, std::chrono::duration_cast<std::chrono::steady_clock::duration>(kRetryDelay));
  std::unique_lock<std::mutex> lock(cancellation_mutex_);
  const bool cancelled = cancellation_changed_.wait_until(
      lock, std::chrono::system_clock::now() + delay, [this, generation] {
        return cancellation_generation_.load() != generation;
      });
  return !cancelled && std::chrono::steady_clock::now() < deadline;
}

}  // namespace voice
}  // namespace cockpit
