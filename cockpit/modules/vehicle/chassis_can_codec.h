#pragma once

#include <cstdint>

#include "cockpit/modules/can/can_frame.h"

namespace cockpit::vehicle {

struct ChassisVelocityCommand {
  bool enabled = false;
  std::uint8_t sequence = 0;
  std::int16_t linear_velocity_mm_s = 0;
  std::int16_t angular_velocity_mrad_s = 0;
};

struct ChassisMotionStatus {
  bool valid = false;
  bool running = false;
  std::uint8_t sequence = 0;
  std::uint8_t control_state = 0;
  std::int16_t left_velocity_mm_s = 0;
  std::int16_t right_velocity_mm_s = 0;
  std::int16_t linear_velocity_mm_s = 0;
  std::int16_t angular_velocity_mrad_s = 0;
  std::int16_t left_output_permille = 0;
  std::int16_t right_output_permille = 0;
};

struct ChassisOdometryReport {
  bool valid = false;
  std::uint8_t sequence = 0;
  std::uint32_t timestamp_ms = 0;
  std::int32_t x_mm = 0;
  std::int32_t y_mm = 0;
  std::int32_t heading_mrad = 0;
  std::int16_t linear_velocity_mm_s = 0;
  std::int16_t angular_velocity_mrad_s = 0;
};

struct ChassisHeartbeat {
  std::uint8_t node_state = 0;
  std::uint8_t sequence = 0;
  std::uint8_t flags = 0;
  std::uint32_t uptime_ms = 0;
  std::uint16_t fault_summary = 0;
};

enum class ChassisHeartbeatStatus {
  kUnknown,
  kAlive,
  kTimeout,
};

struct ChassisHeartbeatSnapshot {
  ChassisHeartbeatStatus status = ChassisHeartbeatStatus::kUnknown;
  ChassisHeartbeat heartbeat;
  std::int64_t received_ms = 0;
  std::int64_t age_ms = 0;
  std::uint64_t peer_reboot_count = 0;
};

struct ChassisFaultStatus {
  std::uint8_t severity = 0;
  std::uint8_t sequence = 0;
  std::uint8_t flags = 0;
  std::uint32_t active_faults = 0;
  std::uint32_t latched_faults = 0;
  std::uint16_t fault_sequence = 0;
};

class ChassisCanCodec {
 public:
  static constexpr std::uint32_t kVelocityCommandId = 0x101U;
  static constexpr std::uint32_t kMotionStatusId = 0x180U;
  static constexpr std::uint32_t kOdometryReportId = 0x181U;
  static constexpr std::uint32_t kHeartbeatId = 0x200U;
  static constexpr std::uint32_t kFaultStatusId = 0x240U;
  static constexpr std::uint32_t kHandshakeRequestId = 0x720U;
  static constexpr std::uint32_t kHandshakeResponseId = 0x721U;

  static can::CanFrame EncodeHandshakePing();
  static can::CanFrame EncodeHandshakePass();
  static bool DecodeHandshakeResponse(const can::CanFrame& frame);
  static bool EncodeVelocity(const ChassisVelocityCommand& command, can::CanFrame* frame);
  static bool EncodeHeartbeat(const ChassisHeartbeat& heartbeat, can::CanFrame* frame);
  static bool DecodeMotion(const can::CanFrame& frame, ChassisMotionStatus* status);
  static bool DecodeOdometry(const can::CanFrame& frame, ChassisOdometryReport* report);
  static bool DecodeHeartbeat(const can::CanFrame& frame, ChassisHeartbeat* heartbeat);
  static bool DecodeFault(const can::CanFrame& frame, ChassisFaultStatus* status);
  static std::uint16_t Crc16(std::uint16_t identifier, const std::uint8_t* payload,
                             std::uint8_t length);
};

class ChassisHeartbeatMonitor {
 public:
  static constexpr std::int64_t kTimeoutMs = 300;

  bool Process(const can::CanFrame& frame, std::int64_t now_ms);
  void Update(std::int64_t now_ms);
  ChassisHeartbeatSnapshot GetSnapshot(std::int64_t now_ms) const;

 private:
  ChassisHeartbeatSnapshot snapshot_;
  bool sequence_valid_ = false;
  std::uint8_t last_sequence_ = 0;
};

}  // namespace cockpit::vehicle
