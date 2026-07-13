#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "cockpit/library/transfer/gateway_grpc_service.h"
#include "cockpit/library/transfer/vehicle_state_client.h"

namespace cockpit {
namespace transfer {

class TransferRuntime {
 public:
  TransferRuntime() = default;
  ~TransferRuntime();

  TransferRuntime(const TransferRuntime&) = delete;
  TransferRuntime& operator=(const TransferRuntime&) = delete;

  bool Start(const std::string& config_path, int samples = 0, int max_hz = 10);
  void Stop();
  int Poll() const;

 private:
  gateway::GatewayGrpcService gateway_service_;
  std::unique_ptr<gateway::VehicleStateClient> vehicle_client_;
  std::thread worker_;
  std::atomic_bool stopping_{false};
  std::atomic_bool running_{false};
  std::atomic_int result_{0};
};

}  // namespace transfer
}  // namespace cockpit
