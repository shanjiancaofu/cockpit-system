#pragma once

#include "core/runtime/ServiceRuntime.h"
#include "modules/vehicle/VehicleState.h"

namespace cockpit {
namespace vehicle {

class VehicleDataService {
 public:
  explicit VehicleDataService(runtime::ServiceRuntime& runtime);

  int Run();

 private:
  int RunMock();
  int RunSocketCan();
  void Publish(const VehicleState& state) const;

  runtime::ServiceRuntime& runtime_;
};

}  // namespace vehicle
}  // namespace cockpit
