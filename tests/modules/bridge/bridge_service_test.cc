#include "cockpit/modules/bridge/bridge_service.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "cockpit/modules/bridge/fake_bridge_provider.h"

namespace {

using cockpit::bridge::BridgeService;
using cockpit::bridge::FakeBridgeOutcome;
using cockpit::bridge::IsActiveNavigationState;
using cockpit::bridge::NavigationGoal;
using cockpit::bridge::NavigationProvider;
using cockpit::bridge::NavigationState;
using cockpit::bridge::NavigationStatus;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

NavigationGoal Goal(std::string id = "goal-1") {
  NavigationGoal goal;
  goal.goal_id = std::move(id);
  goal.target.x_m = 1.5;
  goal.target.y_m = -2.0;
  goal.target.yaw_rad = 0.5;
  goal.target.frame_id = "map";
  return goal;
}

class DelayedCancelProvider final : public NavigationProvider {
 public:
  NavigationStatus SubmitNavigationGoal(const NavigationGoal& goal) override {
    status_ = {};
    status_.state = NavigationState::kAccepted;
    status_.goal_id = goal.goal_id;
    status_.target = goal.target;
    return status_;
  }

  NavigationStatus CancelNavigationGoal(const std::string&) override {
    ++cancel_count;
    cancel_pending_ = true;
    cancel_poll_count_ = 0;
    status_.state = NavigationState::kExecuting;
    status_.last_error = "fake cancellation not confirmed";
    return status_;
  }

  NavigationStatus GetNavigationStatus() override {
    if (status_.goal_id.empty()) {
      status_.state = NavigationState::kIdle;
    } else if (!cancel_pending_) {
      status_.state = NavigationState::kExecuting;
    } else if (++cancel_poll_count_ >= 2) {
      status_.state = NavigationState::kCancelled;
      status_.last_error.clear();
    }
    return status_;
  }

  int cancel_count = 0;

 private:
  NavigationStatus status_;
  bool cancel_pending_ = false;
  int cancel_poll_count_ = 0;
};

}  // namespace

int main() {
  std::int64_t now_ms = 1000;
  NavigationStatus status;
  std::string error;

  BridgeService success(
      cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kSucceeded), 1000,
      [&] {
        return now_ms;
      },
      [&] {
        return now_ms;
      });
  Require(success.SubmitNavigationGoal(Goal(), &status, &error) &&
              status.state == NavigationState::kAccepted && status.accepted_at_ms == 1000 &&
              !status.current_pose_valid,
          "successful fake goal was not accepted");
  Require(status.goal_id == "goal-1" && status.target.frame_id == "map",
          "goal fields were not preserved");
  status = success.GetNavigationStatus();
  Require(status.state == NavigationState::kExecuting && status.current_pose_valid &&
              status.accepted_at_ms == 1000 && status.updated_at_ms == now_ms,
          "accepted goal did not begin executing");
  status = success.GetNavigationStatus();
  Require(status.state == NavigationState::kSucceeded && status.current_pose_valid &&
              status.current_pose.x_m == 1.5,
          "fake goal did not succeed");

  BridgeService cancelled(
      cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kStalled), 1000, [&] {
        return now_ms;
      });
  Require(cancelled.SubmitNavigationGoal(Goal("cancel-me"), &status, &error),
          "cancel fixture submit failed");
  Require(cancelled.CancelNavigationGoal("cancel-me", &status, &error) &&
              status.state == NavigationState::kCancelled,
          "active goal was not cancelled");
  Require(!cancelled.CancelNavigationGoal("cancel-me", &status, &error) &&
              error == "no active bridge goal",
          "terminal goal accepted duplicate cancellation");

  BridgeService timeout(cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kStalled),
                        100, [&] {
                          return now_ms;
                        });
  Require(timeout.SubmitNavigationGoal(Goal("timeout"), &status, &error),
          "timeout fixture submit failed");
  now_ms += 100;
  status = timeout.GetNavigationStatus();
  Require(status.state == NavigationState::kTimedOut, "stalled bridge goal did not time out");
  Require(timeout.GetNavigationStatus().state == NavigationState::kTimedOut,
          "timed-out terminal state was not latched");

  std::int64_t steady_ms = 5000;
  std::int64_t wall_ms = 20000;
  BridgeService wall_rollback_timeout(
      cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kStalled), 100,
      [&] {
        return steady_ms;
      },
      [&] {
        return wall_ms;
      });
  Require(wall_rollback_timeout.SubmitNavigationGoal(Goal("wall-rollback"), &status, &error) &&
              status.accepted_at_ms == wall_ms,
          "wall rollback timeout fixture submit failed");
  steady_ms += 100;
  wall_ms -= 10000;
  Require(wall_rollback_timeout.GetNavigationStatus().state == NavigationState::kTimedOut,
          "wall clock rollback postponed the monotonic goal timeout");

  auto delayed_cancel_provider = std::make_unique<DelayedCancelProvider>();
  auto* delayed_cancel_provider_ptr = delayed_cancel_provider.get();
  BridgeService delayed_timeout(
      std::move(delayed_cancel_provider), 100,
      [&] {
        return now_ms;
      },
      [&] {
        return now_ms + 10000;
      });
  Require(delayed_timeout.SubmitNavigationGoal(Goal("delayed-timeout"), &status, &error),
          "delayed timeout fixture submit failed");
  now_ms += 100;
  status = delayed_timeout.GetNavigationStatus();
  Require(IsActiveNavigationState(status.state) &&
              status.last_error == "goal timeout; cancellation not confirmed" &&
              delayed_cancel_provider_ptr->cancel_count == 1,
          "unconfirmed timeout cancellation did not retain the active guard");
  status = delayed_timeout.GetNavigationStatus();
  Require(IsActiveNavigationState(status.state) && delayed_cancel_provider_ptr->cancel_count == 1,
          "pending timeout cancellation was retried or terminated early");
  status = delayed_timeout.GetNavigationStatus();
  Require(
      status.state == NavigationState::kTimedOut && delayed_cancel_provider_ptr->cancel_count == 1,
      "confirmed timeout cancellation did not become terminal");

  BridgeService rejected(
      cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kRejected), 1000);
  Require(!rejected.SubmitNavigationGoal(Goal("rejected"), &status, &error) &&
              status.state == NavigationState::kRejected,
          "rejected provider goal was accepted");

  BridgeService failed(cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kFailed),
                       1000);
  Require(failed.SubmitNavigationGoal(Goal("failed"), &status, &error),
          "failed fixture submit failed");
  Require(failed.GetNavigationStatus().state == NavigationState::kExecuting,
          "failed fixture did not execute first");
  Require(failed.GetNavigationStatus().state == NavigationState::kFailed,
          "failed fixture did not enter failed state");

  BridgeService disconnected(
      cockpit::bridge::CreateFakeNavigationProvider(FakeBridgeOutcome::kDisconnected), 1000);
  Require(!disconnected.SubmitNavigationGoal(Goal("offline"), &status, &error) &&
              status.state == NavigationState::kDisconnected,
          "disconnected provider did not fail closed");
  Require(disconnected.GetNavigationStatus().state == NavigationState::kIdle,
          "disconnected fake bridge did not recover");

  BridgeService disabled(cockpit::bridge::CreateDisabledNavigationProvider(), 1000);
  Require(!disabled.SubmitNavigationGoal(Goal("disabled"), &status, &error) &&
              status.state == NavigationState::kDisabled,
          "disabled provider accepted a goal");

  NavigationGoal invalid = Goal("../../unsafe");
  Require(!success.SubmitNavigationGoal(invalid, &status, &error), "unsafe goal_id was accepted");
  invalid = Goal("wrong-frame");
  invalid.target.frame_id = "base_link";
  Require(!success.SubmitNavigationGoal(invalid, &status, &error), "non-map goal was accepted");
  invalid = Goal("nan");
  invalid.target.x_m = std::nan("");
  Require(!success.SubmitNavigationGoal(invalid, &status, &error), "NaN goal was accepted");

  std::cout << "bridge service tests passed\n";
  return 0;
}
