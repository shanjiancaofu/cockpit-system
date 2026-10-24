#pragma once

#include <functional>
#include <string>

#include "cockpit/driver/can/can_link_status.h"
#include "cockpit/driver/vehicle/chassis_state.h"
#include "cockpit/driver/vehicle/vehicle_state.h"
#include "cockpit/exchange/foundation/config/system_config.h"

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
  using ChassisStateSink = std::function<void(const ChassisState&)>;
  using ContinueHandler = std::function<bool()>;
  using LinkStatusSink = std::function<void(const can::CanLinkStatus&)>;

  VehicleDataService(VehicleDataOptions options, StateSink state_sink,
                     ContinueHandler should_continue, LinkStatusSink link_status_sink = nullptr,
                     ChassisStateSink chassis_state_sink = nullptr);

  int Run();

 private:
  int RunMock();
  int RunSocketCan();
  void Publish(const VehicleState& state) const;
  void PublishChassis(const ChassisState& state) const;

  VehicleDataOptions options_;
  StateSink state_sink_;
  ContinueHandler should_continue_;
  LinkStatusSink link_status_sink_;
  ChassisStateSink chassis_state_sink_;
};

}  // namespace vehicle
}  // namespace cockpit
