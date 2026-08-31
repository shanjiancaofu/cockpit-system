#include "cockpit/modules/vehicle/chassis_client.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using cockpit::can::CanFrame;
using cockpit::vehicle::ChassisCanCodec;

CanFrame Frame(std::uint32_t id, const std::array<std::uint8_t, 64>& data, std::uint8_t length) {
  return CanFrame(id, data, length, false, false, true, true);
}

std::uint16_t Crc(std::uint32_t id, const std::array<std::uint8_t, 64>& data, std::uint8_t length) {
  return ChassisCanCodec::Crc16(static_cast<std::uint16_t>(id), data.data(), length);
}

void PutU16(std::array<std::uint8_t, 64>* data, std::size_t offset, std::uint16_t value) {
  (*data)[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  (*data)[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void PutU32(std::array<std::uint8_t, 64>* data, std::size_t offset, std::uint32_t value) {
  (*data)[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  (*data)[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  (*data)[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  (*data)[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

CanFrame HeartbeatFrame(std::uint8_t sequence, std::uint32_t uptime_ms) {
  std::array<std::uint8_t, 64> data{};
  data[0] = 1U;
  data[1] = 2U;
  data[2] = sequence;
  data[3] = 1U;
  PutU32(&data, 4, uptime_ms);
  PutU16(&data, 10, Crc(ChassisCanCodec::kHeartbeatId, data, 10U));
  return Frame(ChassisCanCodec::kHeartbeatId, data, 12U);
}

CanFrame FaultFrame(std::uint8_t sequence, std::uint8_t severity, std::uint32_t active_faults) {
  std::array<std::uint8_t, 64> data{};
  data[0] = 1U;
  data[1] = severity;
  data[2] = sequence;
  data[3] = active_faults == 0U ? 0U : 1U;
  PutU32(&data, 4, active_faults);
  PutU32(&data, 8, active_faults);
  PutU16(&data, 12, active_faults == 0U ? 0U : 1U);
  PutU16(&data, 14, Crc(ChassisCanCodec::kFaultStatusId, data, 14U));
  return Frame(ChassisCanCodec::kFaultStatusId, data, 16U);
}

}  // namespace

int main() {
  using cockpit::vehicle::ChassisClient;
  using cockpit::vehicle::ChassisClientDecodeStatus;
  using cockpit::vehicle::ChassisHeartbeatStatus;
  using cockpit::vehicle::ChassisState;

  constexpr std::int64_t started_ms = 1000;
  ChassisClient client(started_ms);
  ChassisState state;
  CanFrame heartbeat;
  if (!client.HeartbeatDue(started_ms) || !client.BuildHeartbeat(started_ms, &heartbeat) ||
      heartbeat.id() != ChassisCanCodec::kHeartbeatId || client.HeartbeatDue(started_ms + 50) ||
      !client.HeartbeatDue(started_ms + 100)) {
    std::cerr << "Jetson heartbeat scheduling failed\n";
    return 1;
  }

  std::array<std::uint8_t, 64> data{};
  data[0] = 1U;
  data[1] = 3U;
  data[2] = 4U;
  data[3] = 1U;
  PutU16(&data, 4, 100U);
  PutU16(&data, 6, static_cast<std::uint16_t>(-100));
  PutU16(&data, 8, 50U);
  PutU16(&data, 10, static_cast<std::uint16_t>(-250));
  PutU16(&data, 12, 250U);
  PutU16(&data, 14, static_cast<std::uint16_t>(-250));
  if (client.ProcessFrame(Frame(0x180U, data, 16U), 2000, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      !state.motion_valid || !state.running || state.left_velocity_mm_s != 100 ||
      state.right_velocity_mm_s != -100) {
    std::cerr << "motion state aggregation failed\n";
    return 1;
  }

  data = {};
  data[0] = 1U;
  data[1] = 1U;
  data[2] = 5U;
  PutU32(&data, 4, 123456U);
  PutU32(&data, 8, 1000U);
  PutU32(&data, 12, static_cast<std::uint32_t>(-1000));
  PutU32(&data, 16, 1571U);
  PutU16(&data, 20, 250U);
  PutU16(&data, 22, static_cast<std::uint16_t>(-125));
  if (client.ProcessFrame(Frame(0x181U, data, 24U), 2010, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      !state.odometry_valid || state.x_mm != 1000 || state.y_mm != -1000 ||
      state.heading_mrad != 1571 || state.odometry_linear_velocity_mm_s != 250 ||
      state.odometry_angular_velocity_mrad_s != -125) {
    std::cerr << "odometry state aggregation failed\n";
    return 1;
  }

  data = {};
  data[0] = 1U;
  data[1] = 2U;
  data[2] = 9U;
  data[3] = 1U;
  PutU32(&data, 4, 555U);
  PutU16(&data, 8, 0x20U);
  PutU16(&data, 10, Crc(0x200U, data, 10U));
  if (client.ProcessFrame(Frame(0x200U, data, 12U), 2020, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.heartbeat_status != ChassisHeartbeatStatus::kAlive || state.node_state != 2U ||
      state.fault_summary != 0x20U) {
    std::cerr << "heartbeat state aggregation failed\n";
    return 1;
  }
  if (client.Update(2319, &state) || !client.Update(2320, &state) ||
      state.heartbeat_status != ChassisHeartbeatStatus::kTimeout) {
    std::cerr << "heartbeat timeout transition failed\n";
    return 1;
  }

  data = {};
  data[0] = 1U;
  data[1] = 1U;
  data[2] = 11U;
  data[3] = 0U;
  if (client.ProcessFrame(Frame(0x180U, data, 16U), 2330, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.heartbeat_status != ChassisHeartbeatStatus::kTimeout) {
    std::cerr << "non-heartbeat traffic hid heartbeat timeout\n";
    return 1;
  }

  data = {};
  data[0] = 1U;
  data[1] = 2U;
  data[2] = 10U;
  data[3] = 3U;
  PutU32(&data, 4, 0x20U);
  PutU32(&data, 8, 0x30U);
  PutU16(&data, 12, 5U);
  PutU16(&data, 14, Crc(0x240U, data, 14U));
  if (client.ProcessFrame(Frame(0x240U, data, 16U), 2330, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.active_faults != 0x20U || state.latched_faults != 0x30U || state.fault_sequence != 5U ||
      state.ToJson().find("heartbeat_status") == std::string::npos) {
    std::cerr << "fault state aggregation failed\n";
    return 1;
  }

  ChassisClient ordering_client(0);
  if (ordering_client.ProcessFrame(HeartbeatFrame(1U, 1000U), 100, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      ordering_client.ProcessFrame(FaultFrame(100U, 2U, 0x20U), 110, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      ordering_client.ProcessFrame(FaultFrame(100U, 0U, 0U), 120, &state) !=
          ChassisClientDecodeStatus::kIgnored ||
      ordering_client.ProcessFrame(FaultFrame(99U, 0U, 0U), 130, &state) !=
          ChassisClientDecodeStatus::kIgnored ||
      state.active_faults != 0x20U) {
    std::cerr << "duplicate or old fault frame replaced the current fault state\n";
    return 1;
  }
  if (!ordering_client.Update(400, &state) ||
      state.heartbeat_status != ChassisHeartbeatStatus::kTimeout ||
      ordering_client.ProcessFrame(FaultFrame(5U, 0U, 0U), 410, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.active_faults != 0U) {
    std::cerr << "peer timeout did not rebuild the fault sequence baseline\n";
    return 1;
  }

  ChassisClient frame_driven_timeout_client(0);
  if (frame_driven_timeout_client.ProcessFrame(HeartbeatFrame(1U, 1000U), 100, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      frame_driven_timeout_client.ProcessFrame(FaultFrame(120U, 2U, 0x20U), 110, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      frame_driven_timeout_client.ProcessFrame(FaultFrame(1U, 0U, 0U), 400, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.heartbeat_status != ChassisHeartbeatStatus::kTimeout || state.active_faults != 0U) {
    std::cerr << "frame processing depended on a prior timeout update call\n";
    return 1;
  }

  ChassisClient reboot_client(0);
  if (reboot_client.ProcessFrame(HeartbeatFrame(20U, 82345U), 100, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      reboot_client.ProcessFrame(FaultFrame(120U, 2U, 0x20U), 110, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      reboot_client.ProcessFrame(HeartbeatFrame(21U, 30U), 200, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.peer_reboot_count != 1U ||
      reboot_client.ProcessFrame(FaultFrame(1U, 0U, 0U), 210, &state) !=
          ChassisClientDecodeStatus::kUpdated ||
      state.active_faults != 0U) {
    std::cerr << "confirmed STM32 reboot did not reset the fault sequence baseline\n";
    return 1;
  }
  std::cout << "chassis client tests passed\n";
  return 0;
}
