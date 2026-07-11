#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace vehicle {

struct VehicleState {
  std::int64_t timestamp_ms = 0;
  double speed_kph = 0.0;
  int gear = 0;
  int soc_percent = 0;
  bool cloud_enabled = false;
  std::string source = "mock";

  std::string ToJson() const;
};

VehicleState MakeMockVehicleState(int sequence);

}  // namespace vehicle
}  // namespace cockpit
