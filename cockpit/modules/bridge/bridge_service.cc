#include "cockpit/modules/bridge/bridge_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit::bridge {

bool IsActiveNavigationState(NavigationState state) {
  return state == NavigationState::kAccepted || state == NavigationState::kExecuting;
}

const char* NavigationStateName(NavigationState state) {
  switch (state) {
    case NavigationState::kDisabled:
      return "DISABLED";
    case NavigationState::kIdle:
      return "IDLE";
    case NavigationState::kAccepted:
      return "ACCEPTED";
    case NavigationState::kExecuting:
      return "EXECUTING";
    case NavigationState::kSucceeded:
      return "SUCCEEDED";
    case NavigationState::kCancelled:
      return "CANCELLED";
    case NavigationState::kRejected:
      return "REJECTED";
    case NavigationState::kFailed:
      return "FAILED";
    case NavigationState::kTimedOut:
      return "TIMED_OUT";
    case NavigationState::kDisconnected:
      return "DISCONNECTED";
  }
  return "UNKNOWN";
}

BridgeService::BridgeService(std::unique_ptr<NavigationProvider> provider,
                             std::int64_t goal_timeout_ms, Clock clock)
    : provider_(std::move(provider)),
      goal_timeout_ms_(goal_timeout_ms),
      clock_(clock == nullptr ? Clock(time::NowMs) : std::move(clock)) {
  status_.state = provider_ == nullptr ? NavigationState::kDisabled : NavigationState::kIdle;
  status_.updated_at_ms = clock_();
}

bool BridgeService::ValidateGoal(const NavigationGoal& goal, std::string* error) {
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

bool BridgeService::SubmitNavigationGoal(const NavigationGoal& goal, NavigationStatus* status,
                                         std::string* error) {
  if (status == nullptr || error == nullptr) return false;
  error->clear();
  if (!ValidateGoal(goal, error)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  RefreshLocked();
  if (provider_ == nullptr || status_.state == NavigationState::kDisabled ||
      status_.state == NavigationState::kDisconnected) {
    *error = "bridge provider is unavailable";
    *status = status_;
    return false;
  }
  if (IsActiveNavigationState(status_.state)) {
    *error = "a bridge goal is already active";
    *status = status_;
    return false;
  }
  status_ = provider_->SubmitNavigationGoal(goal);
  if (IsActiveNavigationState(status_.state)) {
    status_.accepted_at_ms = clock_();
  } else {
    status_.accepted_at_ms = 0;
  }
  status_.updated_at_ms = clock_();
  *status = status_;
  if (status_.state == NavigationState::kRejected ||
      status_.state == NavigationState::kDisconnected ||
      status_.state == NavigationState::kFailed) {
    *error = status_.last_error.empty() ? status_.message : status_.last_error;
    return false;
  }
  return true;
}

bool BridgeService::CancelNavigationGoal(const std::string& goal_id, NavigationStatus* status,
                                         std::string* error) {
  if (status == nullptr || error == nullptr) return false;
  error->clear();
  std::lock_guard<std::mutex> lock(mutex_);
  RefreshLocked();
  if (!IsActiveNavigationState(status_.state)) {
    *error = "no active bridge goal";
    *status = status_;
    return false;
  }
  if (goal_id.empty() || goal_id != status_.goal_id) {
    *error = "cancel goal_id does not match the active goal";
    *status = status_;
    return false;
  }
  const std::int64_t accepted_at_ms = status_.accepted_at_ms;
  status_ = provider_->CancelNavigationGoal(goal_id);
  status_.accepted_at_ms = accepted_at_ms;
  status_.updated_at_ms = clock_();
  *status = status_;
  if (status_.state != NavigationState::kCancelled) {
    *error = status_.last_error.empty() ? "bridge cancel failed" : status_.last_error;
    return false;
  }
  return true;
}

NavigationStatus BridgeService::GetNavigationStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  RefreshLocked();
  return status_;
}

void BridgeService::RefreshLocked() {
  if (provider_ == nullptr) return;
  if (status_.state == NavigationState::kTimedOut) return;
  const std::int64_t now_ms = clock_();
  if (IsActiveNavigationState(status_.state) && status_.accepted_at_ms > 0 &&
      now_ms - status_.accepted_at_ms >= goal_timeout_ms_) {
    static_cast<void>(provider_->CancelNavigationGoal(status_.goal_id));
    status_.state = NavigationState::kTimedOut;
    status_.updated_at_ms = now_ms;
    status_.message = "bridge goal timed out";
    status_.last_error = status_.message;
    return;
  }
  const NavigationStatus refreshed = provider_->GetNavigationStatus();
  if (status_.goal_id.empty() || refreshed.goal_id.empty() ||
      refreshed.goal_id == status_.goal_id) {
    const std::int64_t accepted_at_ms = refreshed.goal_id.empty() ? 0 : status_.accepted_at_ms;
    status_ = refreshed;
    status_.accepted_at_ms = accepted_at_ms;
  }
  status_.updated_at_ms = now_ms;
}

}  // namespace cockpit::bridge
