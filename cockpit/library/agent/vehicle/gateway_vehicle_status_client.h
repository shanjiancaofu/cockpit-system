#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/modules/voice/actions/vehicle_status_provider.h"
#include "gateway.grpc.pb.h"

namespace cockpit {
namespace voice {

class GatewayVehicleStatusClient final : public VehicleStatusProvider {
 public:
  explicit GatewayVehicleStatusClient(const std::string& address);

  bool GetLatest(VehicleStatusSnapshot* status, std::string* error) override;
  void Cancel() override;

 private:
  bool WaitForRetry(std::uint64_t generation);

  std::unique_ptr<proto::gateway::CockpitGateway::Stub> stub_;
  std::atomic<std::uint64_t> cancellation_generation_{0};
  std::mutex cancellation_mutex_;
  std::condition_variable cancellation_changed_;
  grpc::ClientContext* active_context_ = nullptr;
};

}  // namespace voice
}  // namespace cockpit
