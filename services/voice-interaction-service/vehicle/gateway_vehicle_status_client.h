#pragma once

#include <memory>
#include <string>

#include "gateway.grpc.pb.h"
#include "modules/voice/actions/vehicle_status_provider.h"

namespace cockpit {
namespace voice {

class GatewayVehicleStatusClient final : public VehicleStatusProvider {
 public:
  explicit GatewayVehicleStatusClient(const std::string& address);

  bool GetLatest(VehicleStatusSnapshot* status, std::string* error) override;

 private:
  std::unique_ptr<proto::gateway::CockpitGateway::Stub> stub_;
};

}  // namespace voice
}  // namespace cockpit
