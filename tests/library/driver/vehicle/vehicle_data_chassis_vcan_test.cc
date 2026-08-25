#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/library/driver/vehicle/vehicle_data_service.h"
#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"

namespace {

using cockpit::can::CanFrame;
using cockpit::vehicle::ChassisCanCodec;

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

CanFrame Frame(std::uint32_t id, const std::array<std::uint8_t, 64>& data, std::uint8_t length) {
  return CanFrame(id, data, length, false, false, true, true);
}

bool Send(cockpit::can::SocketCan* socket, const CanFrame& frame) {
  std::string error;
  if (!socket->Send(cockpit::can::ToSocketCanFrame(frame), &error)) {
    std::cerr << error << '\n';
    return false;
  }
  return true;
}

bool FakeStm32(const std::string& interface_name) {
  cockpit::can::SocketCan socket;
  std::string error;
  if (!socket.Open(interface_name, &error)) {
    std::cerr << error << '\n';
    return false;
  }
  while (true) {
    cockpit::can::SocketCanFrame received;
    if (socket.Receive(&received, 2000, &error) != cockpit::can::CanIoStatus::kOk) {
      std::cerr << (error.empty() ? "Jetson heartbeat timed out" : error) << '\n';
      return false;
    }
    CanFrame frame;
    if (!cockpit::can::FromSocketCanFrame(received, &frame, &error)) continue;
    cockpit::vehicle::ChassisHeartbeat heartbeat;
    if (!ChassisCanCodec::DecodeHeartbeat(frame, &heartbeat)) continue;
    if (heartbeat.flags != 1U) return false;
    break;
  }

  std::array<std::uint8_t, 64> data{};
  data[0] = 1U;
  data[1] = 3U;
  data[2] = 1U;
  data[3] = 1U;
  PutU16(&data, 4, 100U);
  PutU16(&data, 6, 100U);
  PutU16(&data, 8, 100U);
  PutU16(&data, 12, 250U);
  PutU16(&data, 14, 250U);
  if (!Send(&socket, Frame(0x180U, data, 16U))) return false;

  data = {};
  data[0] = 1U;
  data[1] = 1U;
  data[2] = 2U;
  PutU32(&data, 4, 123456U);
  PutU32(&data, 8, 1000U);
  PutU32(&data, 12, static_cast<std::uint32_t>(-500));
  PutU32(&data, 16, 250U);
  PutU16(&data, 20, 100U);
  if (!Send(&socket, Frame(0x181U, data, 24U))) return false;

  cockpit::vehicle::ChassisHeartbeat stm_heartbeat{2U, 3U, 1U, 5000U, 0x20U};
  CanFrame heartbeat_frame;
  if (!ChassisCanCodec::EncodeHeartbeat(stm_heartbeat, &heartbeat_frame) ||
      !Send(&socket, heartbeat_frame)) {
    return false;
  }

  data = {};
  data[0] = 1U;
  data[1] = 2U;
  data[2] = 4U;
  data[3] = 3U;
  PutU32(&data, 4, 0x20U);
  PutU32(&data, 8, 0x30U);
  PutU16(&data, 12, 5U);
  PutU16(&data, 14, ChassisCanCodec::Crc16(0x240U, data.data(), 14U));
  return Send(&socket, Frame(0x240U, data, 16U));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: vehicle_data_chassis_vcan_test INTERFACE\n";
    return 2;
  }
  const std::string interface_name = argv[1];
  std::atomic_bool running{true};
  std::atomic_bool fake_result{false};
  std::vector<cockpit::vehicle::ChassisState> states;
  cockpit::can::CanLinkStatus final_link;

  std::thread fake([&] {
    fake_result.store(FakeStm32(interface_name));
  });
  cockpit::vehicle::VehicleDataOptions options;
  options.source = "socketcan";
  options.can.interface = interface_name;
  options.can.receive_timeout_ms = 100;
  options.can.max_idle_timeouts = 10;
  options.samples = 4;
  options.forever = false;
  cockpit::vehicle::VehicleDataService service(
      options, nullptr,
      [&] {
        return running.load();
      },
      [&](const cockpit::can::CanLinkStatus& status) {
        final_link = status;
      },
      [&](const cockpit::vehicle::ChassisState& state) {
        states.push_back(state);
      });
  const int result = service.Run();
  running.store(false);
  fake.join();
  if (result != 0 || !fake_result.load() || states.size() != 4 || final_link.decoded_frames != 4) {
    std::cerr << "vehicle chassis SocketCAN flow failed\n";
    return 1;
  }
  const auto& state = states.back();
  if (!state.motion_valid || !state.odometry_valid || !state.running || state.x_mm != 1000 ||
      state.y_mm != -500 ||
      state.heartbeat_status != cockpit::vehicle::ChassisHeartbeatStatus::kAlive ||
      state.active_faults != 0x20U || state.latched_faults != 0x30U || state.fault_sequence != 5U) {
    std::cerr << "aggregated chassis product state is invalid\n";
    return 1;
  }
  std::cout << "vehicle chassis vcan runtime test passed\n";
  return 0;
}
