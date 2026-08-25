#include "cockpit/modules/bridge/bridge_service.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "cockpit/modules/bridge/fake_bridge_provider.h"

namespace {

using cockpit::bridge::BridgeGoal;
using cockpit::bridge::BridgeService;
using cockpit::bridge::BridgeState;
using cockpit::bridge::BridgeStatus;
using cockpit::bridge::FakeBridgeOutcome;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

BridgeGoal Goal(std::string id = "goal-1") {
  BridgeGoal goal;
  goal.goal_id = std::move(id);
  goal.target.x_m = 1.5;
  goal.target.y_m = -2.0;
  goal.target.yaw_rad = 0.5;
  goal.target.frame_id = "map";
  return goal;
}

}  // namespace

int main() {
  std::int64_t now_ms = 1000;
  BridgeStatus status;
  std::string error;

  BridgeService success(cockpit::bridge::CreateFakeBridgeProvider(FakeBridgeOutcome::kSucceeded),
                        1000, [&] {
                          return now_ms;
                        });
  Require(success.SubmitGoal(Goal(), &status, &error) && status.state == BridgeState::kAccepted &&
              status.accepted_at_ms == 1000,
          "successful fake goal was not accepted");
  Require(status.goal_id == "goal-1" && status.target.frame_id == "map",
          "goal fields were not preserved");
  Require(success.GetStatus().state == BridgeState::kExecuting,
          "accepted goal did not begin executing");
  status = success.GetStatus();
  Require(status.state == BridgeState::kSucceeded && status.current_pose.x_m == 1.5,
          "fake goal did not succeed");

  BridgeService cancelled(cockpit::bridge::CreateFakeBridgeProvider(FakeBridgeOutcome::kStalled),
                          1000, [&] {
                            return now_ms;
                          });
  Require(cancelled.SubmitGoal(Goal("cancel-me"), &status, &error), "cancel fixture submit failed");
  Require(
      cancelled.CancelGoal("cancel-me", &status, &error) && status.state == BridgeState::kCancelled,
      "active goal was not cancelled");
  Require(!cancelled.CancelGoal("cancel-me", &status, &error) && error == "no active bridge goal",
          "terminal goal accepted duplicate cancellation");

  BridgeService timeout(cockpit::bridge::CreateFakeBridgeProvider(FakeBridgeOutcome::kStalled), 100,
                        [&] {
                          return now_ms;
                        });
  Require(timeout.SubmitGoal(Goal("timeout"), &status, &error), "timeout fixture submit failed");
  now_ms += 100;
  Require(timeout.GetStatus().state == BridgeState::kTimedOut,
          "stalled bridge goal did not time out");

  BridgeService rejected(cockpit::bridge::CreateFakeBridgeProvider(FakeBridgeOutcome::kRejected),
                         1000);
  Require(!rejected.SubmitGoal(Goal("rejected"), &status, &error) &&
              status.state == BridgeState::kRejected,
          "rejected provider goal was accepted");

  BridgeService failed(cockpit::bridge::CreateFakeBridgeProvider(FakeBridgeOutcome::kFailed), 1000);
  Require(failed.SubmitGoal(Goal("failed"), &status, &error), "failed fixture submit failed");
  Require(failed.GetStatus().state == BridgeState::kExecuting,
          "failed fixture did not execute first");
  Require(failed.GetStatus().state == BridgeState::kFailed,
          "failed fixture did not enter failed state");

  BridgeService disconnected(
      cockpit::bridge::CreateFakeBridgeProvider(FakeBridgeOutcome::kDisconnected), 1000);
  Require(!disconnected.SubmitGoal(Goal("offline"), &status, &error) &&
              status.state == BridgeState::kDisconnected,
          "disconnected provider did not fail closed");
  Require(disconnected.GetStatus().state == BridgeState::kIdle,
          "disconnected fake bridge did not recover");

  BridgeService disabled(cockpit::bridge::CreateDisabledBridgeProvider(), 1000);
  Require(!disabled.SubmitGoal(Goal("disabled"), &status, &error) &&
              status.state == BridgeState::kDisabled,
          "disabled provider accepted a goal");

  BridgeGoal invalid = Goal("../../unsafe");
  Require(!success.SubmitGoal(invalid, &status, &error), "unsafe goal_id was accepted");
  invalid = Goal("wrong-frame");
  invalid.target.frame_id = "base_link";
  Require(!success.SubmitGoal(invalid, &status, &error), "non-map goal was accepted");
  invalid = Goal("nan");
  invalid.target.x_m = std::nan("");
  Require(!success.SubmitGoal(invalid, &status, &error), "NaN goal was accepted");

  std::cout << "bridge service tests passed\n";
  return 0;
}
