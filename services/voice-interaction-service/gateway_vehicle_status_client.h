#pragma once

#include "gateway.grpc.pb.h"
#include "modules/voice/vehicle_status_provider.h"

#include <memory>
#include <string>

namespace cockpit {
namespace voice {

class GatewayVehicleStatusClient final : public VehicleStatusProvider {
 public:
  explicit GatewayVehicleStatusClient(const std::string& address);

  bool GetLatest(VehicleStatusSnapshot* status,
                 std::string* error) override;

 private:
  std::unique_ptr<proto::gateway::CockpitGateway::Stub> stub_;
};

}  // namespace voice
}  // namespace cockpit
