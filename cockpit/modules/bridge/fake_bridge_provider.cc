#include "cockpit/modules/bridge/fake_bridge_provider.h"

#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit::bridge {
namespace {

class DisabledNavigationProvider final : public NavigationProvider {
 public:
  NavigationStatus SubmitNavigationGoal(const NavigationGoal&) override {
    return Status();
  }
  NavigationStatus CancelNavigationGoal(const std::string&) override {
    return Status();
  }
  NavigationStatus GetNavigationStatus() override {
    return Status();
  }

 private:
  static NavigationStatus Status() {
    NavigationStatus status;
    status.state = NavigationState::kDisabled;
    status.updated_at_ms = time::NowMs();
    status.message = "bridge provider is disabled";
    return status;
  }
};

class FakeNavigationProvider final : public NavigationProvider {
 public:
  explicit FakeNavigationProvider(FakeBridgeOutcome outcome) : outcome_(outcome) {
    status_.state = NavigationState::kIdle;
  }

  NavigationStatus SubmitNavigationGoal(const NavigationGoal& goal) override {
    status_ = NavigationStatus{};
    status_.goal_id = goal.goal_id;
    status_.target = goal.target;
    status_.current_pose.frame_id = goal.target.frame_id;
    status_.accepted_at_ms = time::NowMs();
    status_.updated_at_ms = status_.accepted_at_ms;
    query_count_ = 0;
    if (outcome_ == FakeBridgeOutcome::kDisconnected) {
      status_.state = NavigationState::kDisconnected;
      status_.message = "fake bridge disconnected";
      status_.last_error = status_.message;
      disconnected_pending_recovery_ = true;
    } else if (outcome_ == FakeBridgeOutcome::kRejected) {
      status_.state = NavigationState::kRejected;
      status_.message = "fake bridge goal rejected";
      status_.last_error = status_.message;
    } else {
      status_.state = NavigationState::kAccepted;
      status_.message = "fake bridge goal accepted";
    }
    return status_;
  }

  NavigationStatus CancelNavigationGoal(const std::string& goal_id) override {
    status_.updated_at_ms = time::NowMs();
    if (!IsActiveNavigationState(status_.state) || goal_id != status_.goal_id) {
      status_.state = NavigationState::kRejected;
      status_.message = "fake bridge cancel rejected";
      status_.last_error = status_.message;
      return status_;
    }
    status_.state = NavigationState::kCancelled;
    status_.message = "fake bridge goal cancelled";
    status_.last_error.clear();
    return status_;
  }

  NavigationStatus GetNavigationStatus() override {
    status_.updated_at_ms = time::NowMs();
    if (status_.state == NavigationState::kDisconnected && disconnected_pending_recovery_) {
      disconnected_pending_recovery_ = false;
      status_ = NavigationStatus{};
      status_.state = NavigationState::kIdle;
      status_.updated_at_ms = time::NowMs();
      status_.message = "fake bridge recovered";
      return status_;
    }
    if (!IsActiveNavigationState(status_.state)) {
      if (status_.state == NavigationState::kDisabled) {
        status_.state = NavigationState::kIdle;
        status_.message = "fake bridge ready";
      }
      return status_;
    }
    ++query_count_;
    if (status_.state == NavigationState::kAccepted) {
      status_.state = NavigationState::kExecuting;
      status_.message = "fake bridge executing";
      status_.current_pose.x_m = status_.target.x_m * 0.5;
      status_.current_pose.y_m = status_.target.y_m * 0.5;
      status_.current_pose.yaw_rad = status_.target.yaw_rad * 0.5;
      status_.current_pose.timestamp_ms = status_.updated_at_ms;
      return status_;
    }
    if (query_count_ < 2 || outcome_ == FakeBridgeOutcome::kStalled) {
      return status_;
    }
    if (outcome_ == FakeBridgeOutcome::kFailed) {
      status_.state = NavigationState::kFailed;
      status_.message = "fake bridge failed";
      status_.last_error = status_.message;
      return status_;
    }
    status_.state = NavigationState::kSucceeded;
    status_.message = "fake bridge succeeded";
    status_.current_pose = status_.target;
    status_.current_pose.timestamp_ms = status_.updated_at_ms;
    status_.last_error.clear();
    return status_;
  }

 private:
  const FakeBridgeOutcome outcome_;
  NavigationStatus status_;
  int query_count_ = 0;
  bool disconnected_pending_recovery_ = false;
};

}  // namespace

bool ParseFakeBridgeOutcome(const std::string& value, FakeBridgeOutcome* outcome) {
  if (outcome == nullptr) return false;
  if (value == "succeeded")
    *outcome = FakeBridgeOutcome::kSucceeded;
  else if (value == "rejected")
    *outcome = FakeBridgeOutcome::kRejected;
  else if (value == "failed")
    *outcome = FakeBridgeOutcome::kFailed;
  else if (value == "stalled")
    *outcome = FakeBridgeOutcome::kStalled;
  else if (value == "disconnected")
    *outcome = FakeBridgeOutcome::kDisconnected;
  else
    return false;
  return true;
}

std::unique_ptr<NavigationProvider> CreateFakeNavigationProvider(FakeBridgeOutcome outcome) {
  return std::make_unique<FakeNavigationProvider>(outcome);
}

std::unique_ptr<NavigationProvider> CreateDisabledNavigationProvider() {
  return std::make_unique<DisabledNavigationProvider>();
}

}  // namespace cockpit::bridge
