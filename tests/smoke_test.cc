#include "common/can/can_frame.h"
#include "common/vehicle/VehicleState.h"

#include <array>
#include <iostream>

int main() {
  const auto state = cockpit::vehicle::MakeMockVehicleState(0);
  const auto json = state.ToJson();
  if (json.find("speed_kph") == std::string::npos) {
    std::cerr << "missing speed_kph in json" << std::endl;
    return 1;
  }

  const std::array<std::uint8_t, cockpit::can::CanFrame::kMaxDataLength> data{
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  const cockpit::can::CanFrame frame(0x123, data, static_cast<std::uint8_t>(data.size()));
  if (!frame.IsValid() || frame.ToString() != "123#0001020304050607") {
    std::cerr << "invalid CAN frame formatting" << std::endl;
    return 1;
  }
  return 0;
}
