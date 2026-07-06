#pragma once

#include <functional>

#include "cockpit/core/runtime/ServiceRuntime.h"
#include "cockpit/modules/vehicle/VehicleState.h"

namespace cockpit {
namespace vehicle {

class VehicleDataService {
 public:
  using StateSink = std::function<void(const VehicleState&)>;

  VehicleDataService(runtime::ServiceRuntime& runtime, StateSink state_sink);

  int Run();

 private:
  int RunMock();
  int RunSocketCan();
  void Publish(const VehicleState& state) const;

  runtime::ServiceRuntime& runtime_;
  StateSink state_sink_;
};

}  // namespace vehicle
}  // namespace cockpit
