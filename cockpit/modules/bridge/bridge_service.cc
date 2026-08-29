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

namespace {

bool IsConfirmedTerminalState(NavigationState state) {
  return state == NavigationState::kCancelled || state == NavigationState::kFailed ||
         state == NavigationState::kSucceeded;
}

}  // namespace

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
                             std::int64_t goal_timeout_ms, Clock steady_clock, Clock wall_clock)
    : provider_(std::move(provider)),
      goal_timeout_ms_(goal_timeout_ms),
      steady_clock_(steady_clock == nullptr ? Clock(time::SteadyNowMs) : std::move(steady_clock)),
      wall_clock_(wall_clock == nullptr ? Clock(time::WallNowMs) : std::move(wall_clock)) {
  status_.state = provider_ == nullptr ? NavigationState::kDisabled : NavigationState::kIdle;
  status_.updated_at_ms = wall_clock_();
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
    accepted_at_steady_ms_ = steady_clock_();
    status_.accepted_at_ms = wall_clock_();
  } else {
    accepted_at_steady_ms_ = 0;
    status_.accepted_at_ms = 0;
  }
  timeout_cancel_requested_ = false;
  status_.updated_at_ms = wall_clock_();
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
  const NavigationStatus active_status = status_;
  const NavigationStatus cancellation = provider_->CancelNavigationGoal(goal_id);
  if (IsConfirmedTerminalState(cancellation.state)) {
    status_ = cancellation;
    accepted_at_steady_ms_ = 0;
    timeout_cancel_requested_ = false;
  } else {
    status_ = active_status;
    status_.last_error = cancellation.last_error.empty() ? "bridge cancellation not confirmed"
                                                         : cancellation.last_error;
  }
  status_.accepted_at_ms = accepted_at_ms;
  status_.updated_at_ms = wall_clock_();
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
  const std::int64_t steady_now_ms = steady_clock_();
  const std::int64_t wall_now_ms = wall_clock_();
  const NavigationStatus previous = status_;
  const NavigationStatus refreshed = provider_->GetNavigationStatus();
  if (status_.goal_id.empty() || refreshed.goal_id.empty() ||
      refreshed.goal_id == status_.goal_id) {
    if (timeout_cancel_requested_ && !IsActiveNavigationState(refreshed.state) &&
        !IsConfirmedTerminalState(refreshed.state)) {
      status_ = previous;
    } else {
      const std::int64_t accepted_at_ms = refreshed.goal_id.empty() ? 0 : status_.accepted_at_ms;
      status_ = refreshed;
      status_.accepted_at_ms = accepted_at_ms;
      if (!IsActiveNavigationState(status_.state)) {
        accepted_at_steady_ms_ = 0;
      }
    }
  }
  if (timeout_cancel_requested_ && IsConfirmedTerminalState(status_.state)) {
    accepted_at_steady_ms_ = 0;
    if (status_.state == NavigationState::kCancelled) {
      status_.state = NavigationState::kTimedOut;
      status_.message = "bridge goal timed out; cancellation confirmed";
      status_.last_error = status_.message;
    }
    status_.updated_at_ms = wall_now_ms;
    return;
  }
  if (IsActiveNavigationState(status_.state) && accepted_at_steady_ms_ > 0 &&
      steady_now_ms - accepted_at_steady_ms_ >= goal_timeout_ms_) {
    if (!timeout_cancel_requested_) {
      const std::int64_t accepted_at_ms = status_.accepted_at_ms;
      const NavigationStatus cancellation = provider_->CancelNavigationGoal(status_.goal_id);
      timeout_cancel_requested_ = true;
      if (IsConfirmedTerminalState(cancellation.state)) {
        status_ = cancellation;
        status_.accepted_at_ms = accepted_at_ms;
        accepted_at_steady_ms_ = 0;
        if (status_.state == NavigationState::kCancelled) {
          status_.state = NavigationState::kTimedOut;
          status_.message = "bridge goal timed out; cancellation confirmed";
          status_.last_error = status_.message;
        }
        status_.updated_at_ms = wall_now_ms;
        return;
      }
    }
    status_.message = "bridge goal timed out; retaining active guard";
    status_.last_error = "goal timeout; cancellation not confirmed";
  }
  status_.updated_at_ms = wall_now_ms;
}

}  // namespace cockpit::bridge
