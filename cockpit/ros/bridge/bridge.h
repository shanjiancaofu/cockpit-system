#pragma once

#include <cstdint>
#include <string>

namespace cockpit::bridge {

enum class NavigationState {
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

struct NavigationPose {
  double x_m = 0.0;
  double y_m = 0.0;
  double yaw_rad = 0.0;
  std::string frame_id = "map";
  std::int64_t timestamp_ms = 0;
};

struct NavigationGoal {
  std::string goal_id;
  NavigationPose target;
};

struct NavigationStatus {
  NavigationState state = NavigationState::kDisabled;
  std::string goal_id;
  NavigationPose target;
  NavigationPose current_pose;
  bool current_pose_valid = false;
  std::int64_t accepted_at_ms = 0;
  std::int64_t updated_at_ms = 0;
  std::string message;
  std::string last_error;
};

bool IsActiveNavigationState(NavigationState state);
const char* NavigationStateName(NavigationState state);

}  // namespace cockpit::bridge
