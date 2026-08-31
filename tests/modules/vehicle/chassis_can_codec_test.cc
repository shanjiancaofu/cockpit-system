#include "cockpit/modules/vehicle/chassis_can_codec.h"

#include <array>
#include <cstdint>
#include <iostream>

#include "cockpit/modules/vehicle/chassis_can_safety_state_source.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using cockpit::can::CanFrame;
  using cockpit::vehicle::ChassisCanCodec;
  using cockpit::vehicle::ChassisFaultStatus;
  using cockpit::vehicle::ChassisHeartbeat;
  using cockpit::vehicle::ChassisHeartbeatMonitor;
  using cockpit::vehicle::ChassisHeartbeatStatus;
  using cockpit::vehicle::ChassisMotionStatus;
  using cockpit::vehicle::ChassisOdometryReport;
  using cockpit::vehicle::ChassisVelocityCommand;

  bool success = true;
  CanFrame velocity;
  success &= Expect(
      ChassisCanCodec::EncodeVelocity(ChassisVelocityCommand{true, 7U, 500, -250}, &velocity),
      "velocity encode failed");
  success &= Expect(velocity.ToString() == "101##101010700F40106FF00000DC9",
                    "velocity golden vector changed");
  success &=
      Expect(!ChassisCanCodec::EncodeVelocity(ChassisVelocityCommand{false, 8U, 1, 0}, &velocity),
             "disabled nonzero velocity accepted");
  success &=
      Expect(ChassisCanCodec::DecodeHandshakeResponse(CanFrame(
                 0x721U, {'C', 'H', 'A', 'S', 'S', 'I', 'S', 1U}, 8U, false, false, true, true)),
             "handshake response rejected");

  std::array<std::uint8_t, CanFrame::kMaxDataLength> motion_data{};
  motion_data[0] = 1U;
  motion_data[1] = 3U;
  motion_data[2] = 4U;
  motion_data[3] = 1U;
  motion_data[4] = 100U;
  motion_data[6] = 0x9CU;
  motion_data[7] = 0xFFU;
  ChassisMotionStatus motion;
  success &= Expect(ChassisCanCodec::DecodeMotion(
                        CanFrame(0x180U, motion_data, 16U, false, false, true, true), &motion) &&
                        motion.valid && motion.running && motion.left_velocity_mm_s == 100 &&
                        motion.right_velocity_mm_s == -100,
                    "motion decode failed");

  std::array<std::uint8_t, CanFrame::kMaxDataLength> odometry_data{};
  odometry_data[0] = 1U;
  odometry_data[1] = 1U;
  odometry_data[2] = 5U;
  odometry_data[4] = 0x78U;
  odometry_data[5] = 0x56U;
  odometry_data[6] = 0x34U;
  odometry_data[7] = 0x12U;
  ChassisOdometryReport odometry;
  success &=
      Expect(ChassisCanCodec::DecodeOdometry(
                 CanFrame(0x181U, odometry_data, 24U, false, false, true, true), &odometry) &&
                 odometry.valid && odometry.timestamp_ms == 0x12345678U,
             "odometry decode failed");

  std::array<std::uint8_t, CanFrame::kMaxDataLength> heartbeat_data{};
  heartbeat_data[0] = 1U;
  heartbeat_data[1] = 2U;
  heartbeat_data[2] = 6U;
  heartbeat_data[3] = 1U;
  const auto heartbeat_crc = ChassisCanCodec::Crc16(0x200U, heartbeat_data.data(), 10U);
  heartbeat_data[10] = static_cast<std::uint8_t>(heartbeat_crc & 0xFFU);
  heartbeat_data[11] = static_cast<std::uint8_t>(heartbeat_crc >> 8U);
  ChassisHeartbeat heartbeat;
  success &=
      Expect(ChassisCanCodec::DecodeHeartbeat(
                 CanFrame(0x200U, heartbeat_data, 12U, false, false, true, true), &heartbeat) &&
                 heartbeat.node_state == 2U && heartbeat.sequence == 6U,
             "heartbeat decode failed");
  CanFrame encoded_heartbeat;
  success &= Expect(ChassisCanCodec::EncodeHeartbeat(ChassisHeartbeat{2U, 9U, 1U, 123456U, 0x30U},
                                                     &encoded_heartbeat),
                    "heartbeat encode failed");
  ChassisHeartbeatMonitor monitor;
  success &=
      Expect(monitor.Process(encoded_heartbeat, 1000), "heartbeat monitor rejected first frame");
  success &= Expect(monitor.GetSnapshot(1299).status == ChassisHeartbeatStatus::kAlive &&
                        monitor.GetSnapshot(1299).age_ms == 299,
                    "heartbeat became stale too early");
  monitor.Update(1300);
  success &= Expect(monitor.GetSnapshot(1300).status == ChassisHeartbeatStatus::kTimeout,
                    "heartbeat timeout was not reported");
  success &=
      Expect(!monitor.Process(encoded_heartbeat, 1400), "duplicate heartbeat revived monitor");
  success &= Expect(monitor.GetSnapshot(1400).status == ChassisHeartbeatStatus::kTimeout,
                    "duplicate heartbeat changed timeout status");
  success &= Expect(
      ChassisCanCodec::EncodeHeartbeat(ChassisHeartbeat{2U, 0U, 1U, 0U, 0U}, &encoded_heartbeat) &&
          monitor.Process(encoded_heartbeat, 1500) &&
          monitor.GetSnapshot(1500).status == ChassisHeartbeatStatus::kAlive &&
          monitor.GetSnapshot(1500).heartbeat.sequence == 0U &&
          monitor.GetSnapshot(1500).peer_reboot_count == 1U,
      "restarted peer heartbeat did not recover monitor");
  ChassisHeartbeatMonitor wrap_monitor;
  success &= Expect(ChassisCanCodec::EncodeHeartbeat(ChassisHeartbeat{2U, 40U, 1U, 0xFFFFFFF0U, 0U},
                                                     &encoded_heartbeat) &&
                        wrap_monitor.Process(encoded_heartbeat, 2000) &&
                        ChassisCanCodec::EncodeHeartbeat(ChassisHeartbeat{2U, 41U, 1U, 0x10U, 0U},
                                                         &encoded_heartbeat) &&
                        wrap_monitor.Process(encoded_heartbeat, 2100) &&
                        wrap_monitor.GetSnapshot(2100).peer_reboot_count == 0U,
                    "uint32 heartbeat uptime wrap was misclassified as a reboot");

  std::array<std::uint8_t, CanFrame::kMaxDataLength> fault_data{};
  fault_data[0] = 1U;
  fault_data[1] = 2U;
  fault_data[2] = 7U;
  fault_data[3] = 3U;
  fault_data[4] = 0x20U;
  fault_data[8] = 0x30U;
  fault_data[12] = 5U;
  const auto fault_crc = ChassisCanCodec::Crc16(0x240U, fault_data.data(), 14U);
  fault_data[14] = static_cast<std::uint8_t>(fault_crc & 0xFFU);
  fault_data[15] = static_cast<std::uint8_t>(fault_crc >> 8U);
  ChassisFaultStatus fault;
  success &= Expect(ChassisCanCodec::DecodeFault(
                        CanFrame(0x240U, fault_data, 16U, false, false, true, true), &fault) &&
                        fault.active_faults == 0x20U && fault.latched_faults == 0x30U &&
                        fault.fault_sequence == 5U,
                    "fault decode failed");
  fault_data[4] ^= 1U;
  success &= Expect(!ChassisCanCodec::DecodeFault(
                        CanFrame(0x240U, fault_data, 16U, false, false, true, true), &fault),
                    "fault CRC corruption accepted");
  std::array<std::uint8_t, CanFrame::kMaxDataLength> clear_fault_data{};
  clear_fault_data[0] = 1U;
  clear_fault_data[1] = 0U;
  clear_fault_data[2] = 8U;
  const auto clear_fault_crc = ChassisCanCodec::Crc16(0x240U, clear_fault_data.data(), 14U);
  clear_fault_data[14] = static_cast<std::uint8_t>(clear_fault_crc & 0xFFU);
  clear_fault_data[15] = static_cast<std::uint8_t>(clear_fault_crc >> 8U);
  cockpit::vehicle::ChassisCanSafetyStateSource safety_source(0);
  success &=
      Expect(ChassisCanCodec::DecodeFault(
                 CanFrame(0x240U, clear_fault_data, 16U, false, false, true, true), &fault) &&
                 fault.severity == 0U && fault.active_faults == 0U && fault.latched_faults == 0U,
             "healthy fault frame was malformed");
  success &= Expect(safety_source.ProcessFrame(encoded_heartbeat, 1000) ==
                        cockpit::vehicle::ChassisClientDecodeStatus::kUpdated,
                    "CAN safety source rejected heartbeat");
  auto safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1100);
  success &= Expect(safety_state.peer_alive && safety_state.chassis_fault,
                    "CAN safety source did not fail closed before fault sample");
  success &= Expect(
      safety_source.ProcessFrame(CanFrame(0x240U, clear_fault_data, 16U, false, false, true, true),
                                 1100) == cockpit::vehicle::ChassisClientDecodeStatus::kUpdated,
      "CAN safety source rejected fault sample");
  safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1100);
  success &= Expect(!safety_state.chassis_fault, "healthy CAN fault sample did not clear fault");
  success &= Expect(
      ChassisCanCodec::EncodeHeartbeat(ChassisHeartbeat{2U, 42U, 1U, 0U, 0U}, &encoded_heartbeat) &&
          safety_source.ProcessFrame(encoded_heartbeat, 1200) ==
              cockpit::vehicle::ChassisClientDecodeStatus::kUpdated,
      "CAN safety source rejected reboot heartbeat evidence");
  safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1200);
  success &= Expect(safety_state.peer_alive && safety_state.chassis_fault,
                    "STM32 reboot did not invalidate the previous fault sample");
  clear_fault_data[2] = 1U;
  const auto post_reboot_fault_crc = ChassisCanCodec::Crc16(0x240U, clear_fault_data.data(), 14U);
  clear_fault_data[14] = static_cast<std::uint8_t>(post_reboot_fault_crc & 0xFFU);
  clear_fault_data[15] = static_cast<std::uint8_t>(post_reboot_fault_crc >> 8U);
  success &= Expect(
      safety_source.ProcessFrame(CanFrame(0x240U, clear_fault_data, 16U, false, false, true, true),
                                 1210) == cockpit::vehicle::ChassisClientDecodeStatus::kUpdated,
      "post-reboot fault sequence baseline was not rebuilt");
  safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1210);
  success &= Expect(!safety_state.chassis_fault, "fresh post-reboot healthy fault was ignored");
  safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1500);
  success &= Expect(safety_state.peer_alive, "CAN heartbeat became stale before 300 ms");
  safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1510);
  success &= Expect(!safety_state.peer_alive, "stale CAN heartbeat remained alive");
  success &= Expect(ChassisCanCodec::EncodeHeartbeat(ChassisHeartbeat{2U, 43U, 1U, 100U, 0U},
                                                     &encoded_heartbeat) &&
                        safety_source.ProcessFrame(encoded_heartbeat, 1600) ==
                            cockpit::vehicle::ChassisClientDecodeStatus::kUpdated,
                    "CAN safety source did not recover after peer timeout");
  safety_state = safety_source.Evaluate(cockpit::vehicle::ChassisSafetyState{}, 1600);
  success &= Expect(safety_state.peer_alive && safety_state.chassis_fault,
                    "peer timeout did not invalidate the previous fault sample on recovery");
  return success ? 0 : 1;
}
