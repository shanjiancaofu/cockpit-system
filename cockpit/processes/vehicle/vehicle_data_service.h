#pragma once

#include <functional>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/modules/vehicle/vehicle_state.h"

namespace cockpit {
namespace vehicle {

class VehicleDataService {
 public:
  using StateSink = std::function<void(const VehicleState&)>;

  VehicleDataService(runtime::ProcessRuntime& runtime, StateSink state_sink);

  int Run();

 private:
  int RunMock();
  int RunSocketCan();
  void Publish(const VehicleState& state) const;

  runtime::ProcessRuntime& runtime_;
  StateSink state_sink_;
};

}  // namespace vehicle
}  // namespace cockpit
