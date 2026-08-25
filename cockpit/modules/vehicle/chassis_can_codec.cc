#include "cockpit/modules/vehicle/chassis_can_codec.h"

#include <algorithm>
#include <array>

namespace cockpit::vehicle {
namespace {

constexpr std::uint8_t kSchemaVersion = 1U;

std::uint16_t GetU16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::int16_t GetI16(const std::uint8_t* data) {
  return static_cast<std::int16_t>(GetU16(data));
}

std::uint32_t GetU32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) |
         (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::int32_t GetI32(const std::uint8_t* data) {
  return static_cast<std::int32_t>(GetU32(data));
}

void PutI16(std::uint8_t* data, std::int16_t value) {
  const auto encoded = static_cast<std::uint16_t>(value);
  data[0] = static_cast<std::uint8_t>(encoded & 0xFFU);
  data[1] = static_cast<std::uint8_t>(encoded >> 8U);
}

void PutU16(std::uint8_t* data, std::uint16_t value) {
  data[0] = static_cast<std::uint8_t>(value & 0xFFU);
  data[1] = static_cast<std::uint8_t>(value >> 8U);
}

void PutU32(std::uint8_t* data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>(value & 0xFFU);
  data[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  data[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  data[3] = static_cast<std::uint8_t>(value >> 24U);
}

can::CanFrame Handshake(const std::array<std::uint8_t, 8>& payload) {
  std::array<std::uint8_t, can::CanFrame::kMaxDataLength> data{};
  std::copy(payload.begin(), payload.end(), data.begin());
  return can::CanFrame(ChassisCanCodec::kHandshakeRequestId, data, 8U, false, false, true, true);
}

bool IsFrame(const can::CanFrame& frame, std::uint32_t id, std::uint8_t length) {
  return frame.id() == id && !frame.extended() && !frame.remote() && frame.fd() && frame.brs() &&
         frame.data_length() == length && frame.data()[0] == kSchemaVersion;
}

}  // namespace

can::CanFrame ChassisCanCodec::EncodeHandshakePing() {
  return Handshake({'P', 'I', 'N', 'G', 1U, 0U, 0U, 0U});
}

can::CanFrame ChassisCanCodec::EncodeHandshakePass() {
  return Handshake({'P', 'A', 'S', 'S', 1U, 0U, 0U, 0U});
}

bool ChassisCanCodec::DecodeHandshakeResponse(const can::CanFrame& frame) {
  static constexpr std::array<std::uint8_t, 8> kResponse = {'C', 'H', 'A', 'S', 'S', 'I', 'S', 1U};
  return frame.id() == kHandshakeResponseId && !frame.extended() && !frame.remote() && frame.fd() &&
         frame.brs() && frame.data_length() == kResponse.size() &&
         std::equal(kResponse.begin(), kResponse.end(), frame.data().begin());
}

bool ChassisCanCodec::EncodeVelocity(const ChassisVelocityCommand& command, can::CanFrame* frame) {
  if (frame == nullptr || command.linear_velocity_mm_s < -2000 ||
      command.linear_velocity_mm_s > 2000 || command.angular_velocity_mrad_s < -10000 ||
      command.angular_velocity_mrad_s > 10000 ||
      (!command.enabled &&
       (command.linear_velocity_mm_s != 0 || command.angular_velocity_mrad_s != 0))) {
    return false;
  }
  std::array<std::uint8_t, can::CanFrame::kMaxDataLength> data{};
  data[0] = kSchemaVersion;
  data[1] = command.enabled ? 1U : 0U;
  data[2] = command.sequence;
  PutI16(&data[4], command.linear_velocity_mm_s);
  PutI16(&data[6], command.angular_velocity_mrad_s);
  const std::uint16_t crc = Crc16(kVelocityCommandId, data.data(), 10U);
  data[10] = static_cast<std::uint8_t>(crc & 0xFFU);
  data[11] = static_cast<std::uint8_t>(crc >> 8U);
  *frame = can::CanFrame(kVelocityCommandId, data, 12U, false, false, true, true);
  return true;
}

bool ChassisCanCodec::EncodeHeartbeat(const ChassisHeartbeat& heartbeat, can::CanFrame* frame) {
  if (frame == nullptr || heartbeat.node_state > 5U || (heartbeat.flags & 0xFEU) != 0U) {
    return false;
  }
  std::array<std::uint8_t, can::CanFrame::kMaxDataLength> data{};
  data[0] = kSchemaVersion;
  data[1] = heartbeat.node_state;
  data[2] = heartbeat.sequence;
  data[3] = heartbeat.flags;
  PutU32(&data[4], heartbeat.uptime_ms);
  PutU16(&data[8], heartbeat.fault_summary);
  PutU16(&data[10], Crc16(kHeartbeatId, data.data(), 10U));
  *frame = can::CanFrame(kHeartbeatId, data, 12U, false, false, true, true);
  return true;
}

bool ChassisCanCodec::DecodeMotion(const can::CanFrame& frame, ChassisMotionStatus* status) {
  if (status == nullptr || !IsFrame(frame, kMotionStatusId, 16U) ||
      (frame.data()[1] & 0xFCU) != 0U || frame.data()[3] > 5U) {
    return false;
  }
  const auto& data = frame.data();
  *status = ChassisMotionStatus{
      (data[1] & 1U) != 0U, (data[1] & 2U) != 0U, data[2],          data[3],
      GetI16(&data[4]),     GetI16(&data[6]),     GetI16(&data[8]), GetI16(&data[10]),
      GetI16(&data[12]),    GetI16(&data[14])};
  return status->left_output_permille >= -1000 && status->left_output_permille <= 1000 &&
         status->right_output_permille >= -1000 && status->right_output_permille <= 1000;
}

bool ChassisCanCodec::DecodeOdometry(const can::CanFrame& frame, ChassisOdometryReport* report) {
  if (report == nullptr || !IsFrame(frame, kOdometryReportId, 24U) ||
      (frame.data()[1] & 0xFEU) != 0U || frame.data()[3] != 0U) {
    return false;
  }
  const auto& data = frame.data();
  *report = ChassisOdometryReport{(data[1] & 1U) != 0U, data[2],           GetU32(&data[4]),
                                  GetI32(&data[8]),     GetI32(&data[12]), GetI32(&data[16]),
                                  GetI16(&data[20]),    GetI16(&data[22])};
  return true;
}

bool ChassisCanCodec::DecodeHeartbeat(const can::CanFrame& frame, ChassisHeartbeat* heartbeat) {
  if (heartbeat == nullptr || !IsFrame(frame, kHeartbeatId, 12U) || frame.data()[1] > 5U ||
      (frame.data()[3] & 0xFEU) != 0U ||
      GetU16(&frame.data()[10]) != Crc16(kHeartbeatId, frame.data().data(), 10U)) {
    return false;
  }
  const auto& data = frame.data();
  *heartbeat = ChassisHeartbeat{data[1], data[2], data[3], GetU32(&data[4]), GetU16(&data[8])};
  return true;
}

bool ChassisCanCodec::DecodeFault(const can::CanFrame& frame, ChassisFaultStatus* status) {
  if (status == nullptr || !IsFrame(frame, kFaultStatusId, 16U) || frame.data()[1] > 2U ||
      (frame.data()[3] & 0xFCU) != 0U ||
      GetU16(&frame.data()[14]) != Crc16(kFaultStatusId, frame.data().data(), 14U)) {
    return false;
  }
  const auto& data = frame.data();
  *status = ChassisFaultStatus{data[1],          data[2],          data[3],
                               GetU32(&data[4]), GetU32(&data[8]), GetU16(&data[12])};
  return true;
}

std::uint16_t ChassisCanCodec::Crc16(std::uint16_t identifier, const std::uint8_t* payload,
                                     std::uint8_t length) {
  std::uint16_t crc = 0xFFFFU;
  const std::array<std::uint8_t, 2> header = {static_cast<std::uint8_t>(identifier & 0xFFU),
                                              static_cast<std::uint8_t>(identifier >> 8U)};
  for (std::uint8_t value : header) {
    crc ^= static_cast<std::uint16_t>(value) << 8U;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                                  : static_cast<std::uint16_t>(crc << 1U);
    }
  }
  if (payload == nullptr && length != 0U) {
    return 0U;
  }
  for (std::uint8_t index = 0; index < length; ++index) {
    crc ^= static_cast<std::uint16_t>(payload[index]) << 8U;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                                  : static_cast<std::uint16_t>(crc << 1U);
    }
  }
  return crc;
}

bool ChassisHeartbeatMonitor::Process(const can::CanFrame& frame, std::int64_t now_ms) {
  ChassisHeartbeat heartbeat;
  if (now_ms < 0 || !ChassisCanCodec::DecodeHeartbeat(frame, &heartbeat)) {
    return false;
  }
  const auto delta = static_cast<std::uint8_t>(heartbeat.sequence - last_sequence_);
  if (sequence_valid_ &&
      (delta == 0U || (snapshot_.status != ChassisHeartbeatStatus::kTimeout && delta >= 128U))) {
    return false;
  }
  sequence_valid_ = true;
  last_sequence_ = heartbeat.sequence;
  snapshot_.status = ChassisHeartbeatStatus::kAlive;
  snapshot_.heartbeat = heartbeat;
  snapshot_.received_ms = now_ms;
  snapshot_.age_ms = 0;
  return true;
}

void ChassisHeartbeatMonitor::Update(std::int64_t now_ms) {
  if (snapshot_.status == ChassisHeartbeatStatus::kAlive &&
      now_ms - snapshot_.received_ms >= kTimeoutMs) {
    snapshot_.status = ChassisHeartbeatStatus::kTimeout;
  }
}

ChassisHeartbeatSnapshot ChassisHeartbeatMonitor::GetSnapshot(std::int64_t now_ms) const {
  ChassisHeartbeatSnapshot result = snapshot_;
  if (sequence_valid_ && now_ms >= result.received_ms) {
    result.age_ms = now_ms - result.received_ms;
  }
  return result;
}

}  // namespace cockpit::vehicle
