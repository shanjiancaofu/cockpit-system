#include <cstdlib>
#include <iostream>
#include <string>

#include "cockpit/library/driver/vehicle/socketcan_chassis_sink.h"
#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

cockpit::can::CanFrame Receive(cockpit::can::SocketCan* receiver) {
  cockpit::can::SocketCanFrame raw;
  std::string error;
  Require(receiver->Receive(&raw, 1000, &error) == cockpit::can::CanIoStatus::kOk,
          "did not receive vcan chassis command");
  cockpit::can::CanFrame frame;
  Require(cockpit::can::FromSocketCanFrame(raw, &frame, &error),
          "received invalid vcan chassis command");
  return frame;
}

std::int16_t ReadI16(const std::uint8_t* data) {
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(data[0]) |
                                   static_cast<std::uint16_t>(data[1]) << 8U);
}

void Validate(const cockpit::can::CanFrame& frame, bool enabled, std::uint8_t sequence,
              std::int16_t linear, std::int16_t angular) {
  Require(frame.id() == cockpit::vehicle::ChassisCanCodec::kVelocityCommandId && frame.fd() &&
              frame.brs() && frame.data_length() == 12,
          "unexpected 0x101 CAN FD envelope");
  Require(frame.data()[1] == (enabled ? 1U : 0U) && frame.data()[2] == sequence,
          "0x101 enable or sequence mismatch");
  Require(ReadI16(&frame.data()[4]) == linear && ReadI16(&frame.data()[6]) == angular,
          "0x101 velocity payload mismatch");
  const std::uint16_t expected_crc =
      cockpit::vehicle::ChassisCanCodec::Crc16(frame.id(), frame.data().data(), 10);
  const std::uint16_t actual_crc = static_cast<std::uint16_t>(frame.data()[10]) |
                                   static_cast<std::uint16_t>(frame.data()[11]) << 8U;
  Require(actual_crc == expected_crc, "0x101 CRC mismatch");
}

}  // namespace

int main(int argc, char** argv) {
  Require(argc == 2, "vcan interface argument missing");
  const std::string interface_name = argv[1];
  std::string error;
  Require(cockpit::vehicle::SocketCanChassisSink::OpenVcanOnly("can0", &error) == nullptr,
          "hardware can0 was accepted by VM-only sink");
  Require(cockpit::vehicle::SocketCanChassisSink::OpenHardware(interface_name, &error) == nullptr,
          "hardware sink accepted the VM vcan interface");

  cockpit::can::SocketCan receiver;
  Require(receiver.Open(interface_name, &error), "open vcan receiver failed");
  auto sink = cockpit::vehicle::SocketCanChassisSink::OpenVcanOnly(interface_name, &error);
  Require(sink != nullptr && sink->interface_name() == "vcan0", "open vcan sink failed");

  cockpit::vehicle::SafeChassisCommand command;
  command.enabled = true;
  command.sequence = 254;
  command.linear_velocity_mm_s = 400;
  command.angular_velocity_mrad_s = -1200;
  Require(sink->Send(command, &error), "send enabled safe command failed");
  Validate(Receive(&receiver), true, 254, 400, -1200);

  command.sequence = 255;
  command.linear_velocity_mm_s = 100;
  command.angular_velocity_mrad_s = 200;
  Require(sink->Send(command, &error), "send sequence 255 failed");
  Validate(Receive(&receiver), true, 255, 100, 200);

  command.enabled = false;
  command.sequence = 0;
  command.linear_velocity_mm_s = 321;
  command.angular_velocity_mrad_s = -654;
  Require(sink->Send(command, &error), "send disabled zero command failed");
  Validate(Receive(&receiver), false, 0, 0, 0);

  std::cout << "SocketCAN chassis sink vcan tests passed\n";
  return 0;
}
