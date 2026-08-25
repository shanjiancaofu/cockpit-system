#pragma once

#include <cstdint>

#include "cockpit/modules/can/can_frame.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"
#include "cockpit/modules/vehicle/chassis_state.h"

namespace cockpit::vehicle {

enum class ChassisClientDecodeStatus {
  kUpdated,
  kIgnored,
  kInvalid,
};

class ChassisClient {
 public:
  static constexpr std::int64_t kHeartbeatPeriodMs = 100;

  ChassisClient();

  ChassisClientDecodeStatus ProcessFrame(const can::CanFrame& frame, std::int64_t now_ms,
                                         ChassisState* state);
  bool Update(std::int64_t now_ms, ChassisState* state);
  bool HeartbeatDue(std::int64_t now_ms) const;
  bool BuildHeartbeat(std::int64_t now_ms, can::CanFrame* frame);
  ChassisState GetState(std::int64_t now_ms) const;

 private:
  void RefreshHeartbeat(std::int64_t now_ms);

  ChassisState state_;
  ChassisHeartbeatMonitor heartbeat_monitor_;
  ChassisHeartbeatStatus reported_heartbeat_status_ = ChassisHeartbeatStatus::kUnknown;
  std::int64_t started_ms_ = 0;
  std::int64_t heartbeat_due_ms_ = 0;
  std::uint8_t heartbeat_sequence_ = 0;
};

}  // namespace cockpit::vehicle
