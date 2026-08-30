#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cockpit::vehicle {

enum class ChassisStopReason {
  kNone,
  kDisabled,
  kAuthorityLost,
  kEmergencyStop,
  kPeerUnavailable,
  kChassisFault,
  kNoCommand,
  kInvalidCommand,
  kCommandStale,
  kClockRegression,
};

struct ChassisSafetyPolicy {
  std::int32_t max_linear_velocity_mm_s = 400;
  std::int32_t max_angular_velocity_mrad_s = 1200;
  std::int32_t max_linear_acceleration_mm_s2 = 400;
  std::int32_t max_angular_acceleration_mrad_s2 = 1200;
  std::int64_t command_timeout_ms = 250;
  std::int64_t output_period_ms = 20;

  bool IsValid() const;
};

struct ChassisSafetyState {
  bool enabled = false;
  bool authority_granted = false;
  bool emergency_stop = false;
  bool peer_alive = false;
  bool chassis_fault = false;
};

struct ChassisStateFreshnessPolicy {
  std::int64_t peer_timeout_ms = 300;
  std::int64_t fault_state_timeout_ms = 300;

  bool IsValid() const;
};

class ChassisSafetyStateTracker final {
 public:
  explicit ChassisSafetyStateTracker(ChassisStateFreshnessPolicy policy);

  bool UpdatePeerHeartbeat(std::int64_t steady_now_ms);
  bool UpdateChassisFault(bool faulted, std::int64_t steady_now_ms);
  ChassisSafetyState Evaluate(const ChassisSafetyState& controls, std::int64_t steady_now_ms) const;
  void Reset();

 private:
  ChassisStateFreshnessPolicy policy_;
  std::int64_t last_peer_heartbeat_ms_ = -1;
  std::int64_t last_fault_state_ms_ = -1;
  bool chassis_fault_ = true;
};

struct ChassisVelocityRequest {
  double linear_velocity_m_s = 0.0;
  double angular_velocity_rad_s = 0.0;
};

struct SafeChassisCommand {
  bool enabled = false;
  std::uint8_t sequence = 0;
  std::int16_t linear_velocity_mm_s = 0;
  std::int16_t angular_velocity_mrad_s = 0;
  std::int64_t generated_at_steady_ms = 0;
  ChassisStopReason stop_reason = ChassisStopReason::kNoCommand;
};

class ChassisSafetyAdapter final {
 public:
  explicit ChassisSafetyAdapter(ChassisSafetyPolicy policy);

  bool Submit(const ChassisVelocityRequest& request, const ChassisSafetyState& state,
              std::int64_t steady_now_ms, std::string* error = nullptr);
  void RejectInvalidCommand(std::int64_t steady_now_ms);
  SafeChassisCommand Evaluate(const ChassisSafetyState& state, std::int64_t steady_now_ms);
  void Reset(std::int64_t steady_now_ms);

 private:
  SafeChassisCommand Stop(ChassisStopReason reason, std::int64_t steady_now_ms);

  ChassisSafetyPolicy policy_;
  std::optional<ChassisVelocityRequest> request_;
  std::int64_t last_request_ms_ = 0;
  std::int64_t last_output_ms_ = 0;
  std::int32_t last_linear_mm_s_ = 0;
  std::int32_t last_angular_mrad_s_ = 0;
  std::uint8_t next_sequence_ = 0;
  bool invalid_command_pending_ = false;
};

const char* ToString(ChassisStopReason reason);

}  // namespace cockpit::vehicle
