#include "cockpit/modules/bridge/bridge_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit::bridge {

bool IsActiveBridgeState(BridgeState state) {
  return state == BridgeState::kAccepted || state == BridgeState::kExecuting;
}

const char* BridgeStateName(BridgeState state) {
  switch (state) {
    case BridgeState::kDisabled:
      return "DISABLED";
    case BridgeState::kIdle:
      return "IDLE";
    case BridgeState::kAccepted:
      return "ACCEPTED";
    case BridgeState::kExecuting:
      return "EXECUTING";
    case BridgeState::kSucceeded:
      return "SUCCEEDED";
    case BridgeState::kCancelled:
      return "CANCELLED";
    case BridgeState::kRejected:
      return "REJECTED";
    case BridgeState::kFailed:
      return "FAILED";
    case BridgeState::kTimedOut:
      return "TIMED_OUT";
    case BridgeState::kDisconnected:
      return "DISCONNECTED";
  }
  return "UNKNOWN";
}

BridgeService::BridgeService(std::unique_ptr<BridgeProvider> provider, std::int64_t goal_timeout_ms,
                             Clock clock)
    : provider_(std::move(provider)),
      goal_timeout_ms_(goal_timeout_ms),
      clock_(clock == nullptr ? Clock(time::NowMs) : std::move(clock)) {
  status_.state = provider_ == nullptr ? BridgeState::kDisabled : BridgeState::kIdle;
  status_.updated_at_ms = clock_();
}

bool BridgeService::ValidateGoal(const BridgeGoal& goal, std::string* error) {
  if (goal.goal_id.empty() || goal.goal_id.size() > 64U ||
      !std::all_of(goal.goal_id.begin(), goal.goal_id.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-' ||
               character == '.';
      })) {
    *error = "goal_id must contain 1-64 allowlisted characters";
    return false;
  }
  if (goal.target.frame_id != "map") {
    *error = "bridge target frame_id must be map";
    return false;
  }
  if (!std::isfinite(goal.target.x_m) || !std::isfinite(goal.target.y_m) ||
      !std::isfinite(goal.target.yaw_rad)) {
    *error = "bridge target coordinates must be finite";
    return false;
  }
  return true;
}

bool BridgeService::SubmitGoal(const BridgeGoal& goal, BridgeStatus* status, std::string* error) {
  if (status == nullptr || error == nullptr) return false;
  error->clear();
  if (!ValidateGoal(goal, error)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  RefreshLocked();
  if (provider_ == nullptr || status_.state == BridgeState::kDisabled ||
      status_.state == BridgeState::kDisconnected) {
    *error = "bridge provider is unavailable";
    *status = status_;
    return false;
  }
  if (IsActiveBridgeState(status_.state)) {
    *error = "a bridge goal is already active";
    *status = status_;
    return false;
  }
  status_ = provider_->SubmitGoal(goal);
  if (IsActiveBridgeState(status_.state)) {
    status_.accepted_at_ms = clock_();
  }
  status_.updated_at_ms = clock_();
  *status = status_;
  if (status_.state == BridgeState::kRejected || status_.state == BridgeState::kDisconnected ||
      status_.state == BridgeState::kFailed) {
    *error = status_.last_error.empty() ? status_.message : status_.last_error;
    return false;
  }
  return true;
}

bool BridgeService::CancelGoal(const std::string& goal_id, BridgeStatus* status,
                               std::string* error) {
  if (status == nullptr || error == nullptr) return false;
  error->clear();
  std::lock_guard<std::mutex> lock(mutex_);
  RefreshLocked();
  if (!IsActiveBridgeState(status_.state)) {
    *error = "no active bridge goal";
    *status = status_;
    return false;
  }
  if (goal_id.empty() || goal_id != status_.goal_id) {
    *error = "cancel goal_id does not match the active goal";
    *status = status_;
    return false;
  }
  status_ = provider_->CancelGoal(goal_id);
  status_.updated_at_ms = clock_();
  *status = status_;
  if (status_.state != BridgeState::kCancelled) {
    *error = status_.last_error.empty() ? "bridge cancel failed" : status_.last_error;
    return false;
  }
  return true;
}

BridgeStatus BridgeService::GetStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  RefreshLocked();
  return status_;
}

void BridgeService::RefreshLocked() {
  if (provider_ == nullptr) return;
  const std::int64_t now_ms = clock_();
  if (IsActiveBridgeState(status_.state) && status_.accepted_at_ms > 0 &&
      now_ms - status_.accepted_at_ms >= goal_timeout_ms_) {
    static_cast<void>(provider_->CancelGoal(status_.goal_id));
    status_.state = BridgeState::kTimedOut;
    status_.updated_at_ms = now_ms;
    status_.message = "bridge goal timed out";
    status_.last_error = status_.message;
    return;
  }
  const BridgeStatus refreshed = provider_->GetStatus();
  if (status_.goal_id.empty() || refreshed.goal_id.empty() ||
      refreshed.goal_id == status_.goal_id) {
    status_ = refreshed;
  }
  status_.updated_at_ms = now_ms;
}

}  // namespace cockpit::bridge
