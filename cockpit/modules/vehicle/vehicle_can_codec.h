#pragma once

#include <cstdint>

#include "cockpit/modules/can/can_frame.h"
#include "cockpit/modules/vehicle/VehicleState.h"

namespace cockpit {
namespace vehicle {

enum class VehicleCanDecodeStatus {
  kDecoded,
  kIgnored,
  kInvalid,
};

class VehicleCanCodec {
 public:
  static constexpr std::uint32_t kStateFrameId = 0x123;
  static constexpr std::uint8_t kStateFrameLength = 5;

  static can::CanFrame Encode(const VehicleState& state);
  static VehicleCanDecodeStatus Decode(const can::CanFrame& frame, VehicleState* state);

 private:
  static std::uint16_t EncodeSpeed(double speed_kph);
};

}  // namespace vehicle
}  // namespace cockpit
