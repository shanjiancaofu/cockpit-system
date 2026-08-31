#include "cockpit/library/driver/vehicle/vcan_chassis_safety_loop.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"

namespace {

using cockpit::can::CanFrame;
using cockpit::vehicle::ChassisCanCodec;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
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

CanFrame FaultFrame(std::uint8_t sequence, bool faulted) {
  std::array<std::uint8_t, 64> data{};
  data[0] = 1U;
  data[1] = faulted ? 2U : 0U;
  data[2] = sequence;
  PutU32(&data, 4, faulted ? 1U : 0U);
  PutU32(&data, 8, faulted ? 1U : 0U);
  PutU16(&data, 12, sequence);
  PutU16(&data, 14, ChassisCanCodec::Crc16(ChassisCanCodec::kFaultStatusId, data.data(), 14U));
  return CanFrame(ChassisCanCodec::kFaultStatusId, data, 16U, false, false, true, true);
}

CanFrame OdometryFrame() {
  std::array<std::uint8_t, 64> data{};
  data[0] = 1U;
  data[1] = 1U;
  data[2] = 9U;
  PutU32(&data, 4, 5000U);
  PutU32(&data, 8, 1250U);
  PutU32(&data, 12, static_cast<std::uint32_t>(-500));
  PutU32(&data, 16, 1571U);
  PutU16(&data, 20, 300U);
  PutU16(&data, 22, static_cast<std::uint16_t>(-250));
  return CanFrame(ChassisCanCodec::kOdometryReportId, data, 24U, false, false, true, true);
}

void Send(cockpit::can::SocketCan* socket, const CanFrame& frame) {
  std::string error;
  Require(socket->Send(cockpit::can::ToSocketCanFrame(frame), &error), error.c_str());
}

void SendHealthyState(cockpit::can::SocketCan* socket, std::uint8_t heartbeat_sequence,
                      std::uint8_t fault_sequence) {
  cockpit::vehicle::ChassisHeartbeat heartbeat{2U, heartbeat_sequence, 1U, 1000U, 0U};
  CanFrame heartbeat_frame;
  Require(ChassisCanCodec::EncodeHeartbeat(heartbeat, &heartbeat_frame), "encode heartbeat failed");
  Send(socket, heartbeat_frame);
  Send(socket, FaultFrame(fault_sequence, false));
}

CanFrame ReceiveCommand(cockpit::can::SocketCan* socket) {
  for (int attempt = 0; attempt < 16; ++attempt) {
    cockpit::can::SocketCanFrame socket_frame;
    std::string error;
    Require(socket->Receive(&socket_frame, 1000, &error) == cockpit::can::CanIoStatus::kOk,
            "vcan safety command was not received");
    CanFrame frame;
    Require(cockpit::can::FromSocketCanFrame(socket_frame, &frame, &error), error.c_str());
    if (frame.id() == ChassisCanCodec::kVelocityCommandId) return frame;
  }
  Require(false, "vcan safety command was not found");
  return {};
}

bool CommandEnabled(const CanFrame& frame) {
  return frame.data_length() == 12U && frame.data()[1] == 1U;
}

}  // namespace

int main(int argc, char** argv) {
  Require(argc == 2, "vcan interface argument missing");
  const std::string interface_name = argv[1];
  cockpit::vehicle::ChassisSafetyPolicy policy;
  std::string error;
  Require(
      cockpit::vehicle::VcanChassisSafetyLoop::OpenVcanOnly("can0", policy, 0, &error) == nullptr,
      "VM safety loop accepted real can0");
  auto loop =
      cockpit::vehicle::VcanChassisSafetyLoop::OpenVcanOnly(interface_name, policy, 0, &error);
  Require(loop != nullptr, error.c_str());
  cockpit::can::SocketCan fake_stm32;
  Require(fake_stm32.Open(interface_name, &error), error.c_str());

  cockpit::vehicle::ChassisSafetyState controls;
  controls.enabled = true;
  controls.authority_granted = true;
  SendHealthyState(&fake_stm32, 1U, 1U);
  cockpit::vehicle::SafeChassisCommand command;
  Require(loop->Step(controls, 1000, 100, &command, &error), error.c_str());
  Require(!command.enabled && !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "safety loop moved without a command");

  cockpit::vehicle::ChassisVelocityRequest request;
  request.linear_velocity_m_s = 0.4;
  request.angular_velocity_rad_s = -1.0;
  Require(loop->Submit(request, controls, 1010, &error), error.c_str());
  Require(loop->Step(controls, 1030, 0, &command, &error), error.c_str());
  Require(command.enabled && CommandEnabled(ReceiveCommand(&fake_stm32)),
          "healthy feedback did not permit bounded motion");

  Send(&fake_stm32, FaultFrame(2U, true));
  Require(loop->Step(controls, 1040, 100, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kChassisFault &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "chassis fault did not force zero");

  SendHealthyState(&fake_stm32, 2U, 3U);
  Send(&fake_stm32, OdometryFrame());
  Require(loop->Step(controls, 1050, 100, &command, &error), error.c_str());
  static_cast<void>(ReceiveCommand(&fake_stm32));
  const auto& odometry = loop->chassis_state();
  Require(odometry.odometry_valid && odometry.x_mm == 1250 && odometry.y_mm == -500 &&
              odometry.odometry_linear_velocity_mm_s == 300 &&
              odometry.odometry_angular_velocity_mrad_s == -250,
          "0x181 did not reach aggregated chassis state");
  Require(loop->Submit(request, controls, 1060, &error), error.c_str());
  Require(loop->Step(controls, 1080, 0, &command, &error) && command.enabled,
          "new command after healthy recovery was rejected");
  Require(CommandEnabled(ReceiveCommand(&fake_stm32)), "recovered command was not enabled");

  Require(loop->Step(controls, 1401, 0, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kPeerUnavailable &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "stale heartbeat did not force zero");

  SendHealthyState(&fake_stm32, 3U, 4U);
  Require(loop->Step(controls, 2000, 100, &command, &error), error.c_str());
  static_cast<void>(ReceiveCommand(&fake_stm32));
  Require(loop->Submit(request, controls, 2010, &error), error.c_str());
  Require(loop->Step(controls, 2030, 0, &command, &error) && command.enabled,
          "fresh command was rejected");
  static_cast<void>(ReceiveCommand(&fake_stm32));
  SendHealthyState(&fake_stm32, 4U, 5U);
  Require(loop->Step(controls, 2300, 100, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kCommandStale &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "stale velocity command did not force zero with fresh chassis state");

  cockpit::vehicle::ChassisSafetyState authority_lost = controls;
  authority_lost.authority_granted = false;
  SendHealthyState(&fake_stm32, 5U, 6U);
  Require(loop->Step(authority_lost, 2400, 100, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kAuthorityLost &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "authority loss did not force zero");

  cockpit::vehicle::ChassisSafetyState emergency_stop = controls;
  emergency_stop.emergency_stop = true;
  SendHealthyState(&fake_stm32, 6U, 7U);
  Require(loop->Step(emergency_stop, 2500, 100, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kEmergencyStop &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "emergency stop did not force zero");

  SendHealthyState(&fake_stm32, 7U, 8U);
  Require(loop->Step(controls, 2600, 100, &command, &error), error.c_str());
  static_cast<void>(ReceiveCommand(&fake_stm32));
  request.linear_velocity_m_s = std::numeric_limits<double>::quiet_NaN();
  Require(!loop->Submit(request, controls, 2610, &error), "NaN velocity command was accepted");
  Require(loop->Step(controls, 2620, 0, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kInvalidCommand &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "invalid velocity command did not force zero");

  cockpit::vehicle::ChassisHeartbeat heartbeat{2U, 8U, 1U, 1000U, 0U};
  CanFrame heartbeat_frame;
  Require(ChassisCanCodec::EncodeHeartbeat(heartbeat, &heartbeat_frame), "encode heartbeat failed");
  Send(&fake_stm32, heartbeat_frame);
  Require(loop->Step(controls, 2921, 100, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kChassisFault &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "stale fault sample did not force zero while heartbeat remained fresh");

  Require(loop->Stop(2930, &command, &error), error.c_str());
  Require(!command.enabled && !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "explicit process stop did not send a final zero command");

  auto backlog_loop =
      cockpit::vehicle::VcanChassisSafetyLoop::OpenVcanOnly(interface_name, policy, 0, &error);
  Require(backlog_loop != nullptr, error.c_str());
  SendHealthyState(&fake_stm32, 20U, 20U);
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  Require(backlog_loop->Step(controls, 4000, 100, &command, &error), error.c_str());
  Require(!command.enabled &&
              command.stop_reason == cockpit::vehicle::ChassisStopReason::kPeerUnavailable &&
              !CommandEnabled(ReceiveCommand(&fake_stm32)),
          "queued stale heartbeat incorrectly refreshed peer freshness after a consumer stall");
  SendHealthyState(&fake_stm32, 21U, 200U);
  Require(backlog_loop->Step(controls, 4010, 100, &command, &error), error.c_str());
  static_cast<void>(ReceiveCommand(&fake_stm32));
  Require(backlog_loop->chassis_state().fault_sequence == 200U,
          "stale queued fault frame incorrectly established the ordering baseline");
  request.linear_velocity_m_s = 0.1;
  Require(backlog_loop->Submit(request, controls, 4020, &error), error.c_str());
  Require(backlog_loop->Step(controls, 4040, 0, &command, &error) && command.enabled &&
              CommandEnabled(ReceiveCommand(&fake_stm32)),
          "fresh safety state did not recover after stale backlog was discarded");

  std::cout << "vcan RX to Safety to 0x101 TX tests passed\n";
  return 0;
}
