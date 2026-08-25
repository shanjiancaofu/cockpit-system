#include "cockpit/modules/bridge/fake_bridge_provider.h"

#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit::bridge {
namespace {

class DisabledBridgeProvider final : public BridgeProvider {
 public:
  BridgeStatus SubmitGoal(const BridgeGoal&) override {
    return Status();
  }
  BridgeStatus CancelGoal(const std::string&) override {
    return Status();
  }
  BridgeStatus GetStatus() override {
    return Status();
  }

 private:
  static BridgeStatus Status() {
    BridgeStatus status;
    status.state = BridgeState::kDisabled;
    status.updated_at_ms = time::NowMs();
    status.message = "bridge provider is disabled";
    return status;
  }
};

class FakeBridgeProvider final : public BridgeProvider {
 public:
  explicit FakeBridgeProvider(FakeBridgeOutcome outcome) : outcome_(outcome) {
    status_.state = BridgeState::kIdle;
  }

  BridgeStatus SubmitGoal(const BridgeGoal& goal) override {
    status_ = BridgeStatus{};
    status_.goal_id = goal.goal_id;
    status_.target = goal.target;
    status_.current_pose.frame_id = goal.target.frame_id;
    status_.accepted_at_ms = time::NowMs();
    status_.updated_at_ms = status_.accepted_at_ms;
    query_count_ = 0;
    if (outcome_ == FakeBridgeOutcome::kDisconnected) {
      status_.state = BridgeState::kDisconnected;
      status_.message = "fake bridge disconnected";
      status_.last_error = status_.message;
      disconnected_pending_recovery_ = true;
    } else if (outcome_ == FakeBridgeOutcome::kRejected) {
      status_.state = BridgeState::kRejected;
      status_.message = "fake bridge goal rejected";
      status_.last_error = status_.message;
    } else {
      status_.state = BridgeState::kAccepted;
      status_.message = "fake bridge goal accepted";
    }
    return status_;
  }

  BridgeStatus CancelGoal(const std::string& goal_id) override {
    status_.updated_at_ms = time::NowMs();
    if (!IsActiveBridgeState(status_.state) || goal_id != status_.goal_id) {
      status_.state = BridgeState::kRejected;
      status_.message = "fake bridge cancel rejected";
      status_.last_error = status_.message;
      return status_;
    }
    status_.state = BridgeState::kCancelled;
    status_.message = "fake bridge goal cancelled";
    status_.last_error.clear();
    return status_;
  }

  BridgeStatus GetStatus() override {
    status_.updated_at_ms = time::NowMs();
    if (status_.state == BridgeState::kDisconnected && disconnected_pending_recovery_) {
      disconnected_pending_recovery_ = false;
      status_ = BridgeStatus{};
      status_.state = BridgeState::kIdle;
      status_.updated_at_ms = time::NowMs();
      status_.message = "fake bridge recovered";
      return status_;
    }
    if (!IsActiveBridgeState(status_.state)) {
      if (status_.state == BridgeState::kDisabled) {
        status_.state = BridgeState::kIdle;
        status_.message = "fake bridge ready";
      }
      return status_;
    }
    ++query_count_;
    if (status_.state == BridgeState::kAccepted) {
      status_.state = BridgeState::kExecuting;
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
      status_.state = BridgeState::kFailed;
      status_.message = "fake bridge failed";
      status_.last_error = status_.message;
      return status_;
    }
    status_.state = BridgeState::kSucceeded;
    status_.message = "fake bridge succeeded";
    status_.current_pose = status_.target;
    status_.current_pose.timestamp_ms = status_.updated_at_ms;
    status_.last_error.clear();
    return status_;
  }

 private:
  const FakeBridgeOutcome outcome_;
  BridgeStatus status_;
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

std::unique_ptr<BridgeProvider> CreateFakeBridgeProvider(FakeBridgeOutcome outcome) {
  return std::make_unique<FakeBridgeProvider>(outcome);
}

std::unique_ptr<BridgeProvider> CreateDisabledBridgeProvider() {
  return std::make_unique<DisabledBridgeProvider>();
}

}  // namespace cockpit::bridge
