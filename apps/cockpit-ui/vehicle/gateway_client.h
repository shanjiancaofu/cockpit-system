#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace cockpit {
namespace ui {

class VehicleStateModel;

class GatewayClient {
 public:
  GatewayClient(std::string address, VehicleStateModel* model);
  ~GatewayClient();

  GatewayClient(const GatewayClient&) = delete;
  GatewayClient& operator=(const GatewayClient&) = delete;

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
