#pragma once

#include "modules/vehicle/VehicleState.h"
#include "vehicle_state.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace cockpit {
namespace vehicle {

class VehicleGrpcService final : public proto::vehicle::VehicleDataService::Service {
 public:
  VehicleGrpcService() = default;
  ~VehicleGrpcService() override;

  VehicleGrpcService(const VehicleGrpcService&) = delete;
  VehicleGrpcService& operator=(const VehicleGrpcService&) = delete;

  bool Start(const std::string& address);
  void Publish(const VehicleState& state);
  void Shutdown();

 private:
  grpc::Status SubscribeVehicleState(
      grpc::ServerContext* context,
      const proto::vehicle::SubscribeVehicleStateRequest* request,
      grpc::ServerWriter<proto::vehicle::VehicleState>* writer) override;

  std::mutex mutex_;
  std::condition_variable state_changed_;
  VehicleState latest_state_;
  std::uint64_t version_ = 0;
  bool stopping_ = false;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace vehicle
}  // namespace cockpit
