#include "cockpit/library/driver/vehicle/socketcan_chassis_sink.h"

#include <utility>

#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"

namespace cockpit::vehicle {

std::unique_ptr<SocketCanChassisSink> SocketCanChassisSink::OpenVcanOnly(
    const std::string& interface_name, std::string* error) {
  if (interface_name != "vcan0") {
    if (error != nullptr) {
      *error = "VM safety baseline only permits the isolated vcan0 interface";
    }
    return nullptr;
  }
  can::SocketCan socket;
  if (!socket.Open(interface_name, error)) return nullptr;
  return std::unique_ptr<SocketCanChassisSink>(
      new SocketCanChassisSink(interface_name, std::move(socket)));
}

std::unique_ptr<SocketCanChassisSink> SocketCanChassisSink::OpenHardware(
    const std::string& interface_name, std::string* error) {
  if (interface_name != "can0") {
    if (error != nullptr)
      *error = "hardware safety baseline only permits the explicit can0 interface";
    return nullptr;
  }
  can::SocketCan socket;
  if (!socket.Open(interface_name, error)) return nullptr;
  return std::unique_ptr<SocketCanChassisSink>(
      new SocketCanChassisSink(interface_name, std::move(socket)));
}

SocketCanChassisSink::SocketCanChassisSink(std::string interface_name, can::SocketCan socket)
    : interface_name_(std::move(interface_name)), socket_(std::move(socket)) {
}

bool SocketCanChassisSink::Send(const SafeChassisCommand& command, std::string* error) {
  ChassisVelocityCommand velocity;
  velocity.enabled = command.enabled;
  velocity.sequence = command.sequence;
  velocity.linear_velocity_mm_s = command.enabled ? command.linear_velocity_mm_s : 0;
  velocity.angular_velocity_mrad_s = command.enabled ? command.angular_velocity_mrad_s : 0;
  can::CanFrame frame;
  if (!ChassisCanCodec::EncodeVelocity(velocity, &frame)) {
    if (error != nullptr) *error = "safe chassis command could not be encoded as CAN 0x101";
    return false;
  }
  return socket_.Send(can::ToSocketCanFrame(frame), error);
}

}  // namespace cockpit::vehicle
