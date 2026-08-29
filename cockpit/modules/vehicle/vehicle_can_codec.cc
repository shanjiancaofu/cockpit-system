#include "cockpit/modules/vehicle/vehicle_can_codec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "cockpit/core/time/time.h"

namespace cockpit {
namespace vehicle {

can::CanFrame VehicleCanCodec::Encode(const VehicleState& state) {
  const std::uint16_t speed = EncodeSpeed(state.speed_kph);
  std::array<std::uint8_t, can::CanFrame::kMaxDataLength> data{};
  data[0] = static_cast<std::uint8_t>(speed & 0xFFU);
  data[1] = static_cast<std::uint8_t>((speed >> 8U) & 0xFFU);
  data[2] = static_cast<std::uint8_t>(std::clamp(state.gear, 0, 255));
  data[3] = static_cast<std::uint8_t>(std::clamp(state.soc_percent, 0, 100));
  data[4] = state.cloud_enabled ? 0x01U : 0x00U;
  return can::CanFrame(kStateFrameId, data, kStateFrameLength);
}

VehicleCanDecodeStatus VehicleCanCodec::Decode(const can::CanFrame& frame, VehicleState* state) {
  if (frame.id() != kStateFrameId || frame.extended()) {
    return VehicleCanDecodeStatus::kIgnored;
  }
  if (state == nullptr || frame.remote() || frame.data_length() < kStateFrameLength) {
    return VehicleCanDecodeStatus::kInvalid;
  }

  const auto& data = frame.data();
  const std::uint16_t speed =
      static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8U);

  state->timestamp_ms = time::WallNowMs();
  state->speed_kph = static_cast<double>(speed) / 10.0;
  state->gear = data[2];
  state->soc_percent = std::min(static_cast<int>(data[3]), 100);
  state->cloud_enabled = (data[4] & 0x01U) != 0;
  state->source = "socketcan";
  return VehicleCanDecodeStatus::kDecoded;
}

std::uint16_t VehicleCanCodec::EncodeSpeed(double speed_kph) {
  const double max_speed = static_cast<double>(std::numeric_limits<std::uint16_t>::max()) / 10.0;
  const double clamped = std::clamp(speed_kph, 0.0, max_speed);
  return static_cast<std::uint16_t>(std::lround(clamped * 10.0));
}

}  // namespace vehicle
}  // namespace cockpit
