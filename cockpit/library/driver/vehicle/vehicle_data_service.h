#pragma once

#include <functional>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/modules/vehicle/vehicle_state.h"

namespace cockpit {
namespace vehicle {

struct VehicleDataOptions {
  config::VehicleDataConfig vehicle;
  config::CanConfig can;
  std::string source;
  int samples{5};
  bool forever{false};
};

class VehicleDataService {
 public:
  using StateSink = std::function<void(const VehicleState&)>;
  using ContinueHandler = std::function<bool()>;

  VehicleDataService(VehicleDataOptions options, StateSink state_sink,
                     ContinueHandler should_continue);

  int Run();

 private:
  int RunMock();
  int RunSocketCan();
  void Publish(const VehicleState& state) const;

  VehicleDataOptions options_;
  StateSink state_sink_;
  ContinueHandler should_continue_;
};

}  // namespace vehicle
}  // namespace cockpit
