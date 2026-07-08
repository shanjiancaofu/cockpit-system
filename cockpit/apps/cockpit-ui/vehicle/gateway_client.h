#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace ui {

class VehicleStateModel;

class GatewayClient {
 public:
  GatewayClient(std::string address, VehicleStateModel* model);
  ~GatewayClient();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(GatewayClient);

  void Start();
  void Stop();

 private:
  void Run();
  void PostConnected(bool connected);

  std::string address_;
  VehicleStateModel* model_;
  std::atomic_bool running_{false};
  std::mutex context_mutex_;
  grpc::ClientContext* active_context_ = nullptr;
  std::thread worker_;
};

}  // namespace ui
}  // namespace cockpit
