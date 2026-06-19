#include "common/vehicle/VehicleState.h"

#include <iostream>

int main() {
  const auto state = cockpit::vehicle::MakeMockVehicleState(0);
  const auto json = state.ToJson();
  if (json.find("speed_kph") == std::string::npos) {
    std::cerr << "missing speed_kph in json" << std::endl;
    return 1;
  }
  return 0;
}
