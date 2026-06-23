#include <array>
#include <iostream>

#include "modules/can/can_frame.h"
#include "modules/vehicle/VehicleState.h"
#include "modules/vehicle/vehicle_can_codec.h"

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

  const auto encoded = cockpit::vehicle::VehicleCanCodec::Encode(state);
  cockpit::vehicle::VehicleState decoded;
  if (cockpit::vehicle::VehicleCanCodec::Decode(encoded, &decoded) !=
          cockpit::vehicle::VehicleCanDecodeStatus::kDecoded ||
      decoded.speed_kph != state.speed_kph || decoded.gear != state.gear ||
      decoded.soc_percent != state.soc_percent || decoded.source != "socketcan") {
    std::cerr << "vehicle CAN codec round trip failed" << std::endl;
    return 1;
  }

  const cockpit::can::CanFrame unrelated(0x456, data, 5);
  if (cockpit::vehicle::VehicleCanCodec::Decode(unrelated, &decoded) !=
      cockpit::vehicle::VehicleCanDecodeStatus::kIgnored) {
    std::cerr << "unrelated CAN frame was not ignored" << std::endl;
    return 1;
  }

  const cockpit::can::CanFrame truncated(cockpit::vehicle::VehicleCanCodec::kStateFrameId, data, 4);
  if (cockpit::vehicle::VehicleCanCodec::Decode(truncated, &decoded) !=
      cockpit::vehicle::VehicleCanDecodeStatus::kInvalid) {
    std::cerr << "truncated vehicle CAN frame was accepted" << std::endl;
    return 1;
  }

  return 0;
}
