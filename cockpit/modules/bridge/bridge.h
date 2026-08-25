#pragma once

#include <cstdint>
#include <string>

namespace cockpit::bridge {

enum class BridgeState {
  kDisabled,
  kIdle,
  kAccepted,
  kExecuting,
  kSucceeded,
  kCancelled,
  kRejected,
  kFailed,
  kTimedOut,
  kDisconnected,
};

struct BridgePose {
  double x_m = 0.0;
  double y_m = 0.0;
  double yaw_rad = 0.0;
  std::string frame_id = "map";
  std::int64_t timestamp_ms = 0;
};

struct BridgeGoal {
  std::string goal_id;
  BridgePose target;
};

struct BridgeStatus {
  BridgeState state = BridgeState::kDisabled;
  std::string goal_id;
  BridgePose target;
  BridgePose current_pose;
  std::int64_t accepted_at_ms = 0;
  std::int64_t updated_at_ms = 0;
  std::string message;
  std::string last_error;
};

bool IsActiveBridgeState(BridgeState state);
const char* BridgeStateName(BridgeState state);

}  // namespace cockpit::bridge
