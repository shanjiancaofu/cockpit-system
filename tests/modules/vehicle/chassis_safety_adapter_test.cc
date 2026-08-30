#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "cockpit/modules/vehicle/chassis_command_sink.h"

namespace {

using cockpit::vehicle::ChassisSafetyAdapter;
using cockpit::vehicle::ChassisSafetyPolicy;
using cockpit::vehicle::ChassisSafetyState;
using cockpit::vehicle::ChassisSafetyStateTracker;
using cockpit::vehicle::ChassisStopReason;
using cockpit::vehicle::ChassisVelocityRequest;
using cockpit::vehicle::SafeChassisCommand;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

class FakeChassisCommandSink final : public cockpit::vehicle::ChassisCommandSink {
 public:
  bool Send(const SafeChassisCommand& command, std::string*) override {
    commands.push_back(command);
    return true;
  }

  std::vector<SafeChassisCommand> commands;
};

ChassisSafetyState Ready() {
  ChassisSafetyState state;
  state.enabled = true;
  state.authority_granted = true;
  state.peer_alive = true;
  return state;
}

}  // namespace

int main() {
  cockpit::vehicle::ChassisStateFreshnessPolicy freshness_policy;
  ChassisSafetyStateTracker state_tracker(freshness_policy);
  auto tracked_state = state_tracker.Evaluate(Ready(), 1000);
  Require(!tracked_state.peer_alive && !tracked_state.chassis_fault,
          "missing peer heartbeat did not fail closed");
  Require(state_tracker.UpdatePeerHeartbeat(1000), "valid peer heartbeat was rejected");
  tracked_state = state_tracker.Evaluate(Ready(), 1000);
  Require(tracked_state.peer_alive && tracked_state.chassis_fault,
          "missing fault sample did not fail closed after peer recovery");
  Require(state_tracker.UpdateChassisFault(false, 1000), "healthy fault sample was rejected");
  tracked_state = state_tracker.Evaluate(Ready(), 1300);
  Require(tracked_state.peer_alive && !tracked_state.chassis_fault,
          "fresh peer/fault state was rejected");
  tracked_state = state_tracker.Evaluate(Ready(), 1301);
  Require(!tracked_state.peer_alive, "stale peer heartbeat remained alive");
  Require(state_tracker.UpdatePeerHeartbeat(1400), "peer heartbeat refresh was rejected");
  tracked_state = state_tracker.Evaluate(Ready(), 1400);
  Require(tracked_state.peer_alive && tracked_state.chassis_fault,
          "stale fault sample did not fail closed");
  Require(!state_tracker.UpdatePeerHeartbeat(1399), "regressing heartbeat timestamp was accepted");
  state_tracker.Reset();

  ChassisSafetyPolicy invalid_policy;
  invalid_policy.command_timeout_ms = 0;
  Require(!invalid_policy.IsValid(), "invalid safety policy was accepted");

  ChassisSafetyPolicy policy;
  policy.max_linear_velocity_mm_s = 400;
  policy.max_angular_velocity_mrad_s = 1200;
  policy.max_linear_acceleration_mm_s2 = 1000;
  policy.max_angular_acceleration_mrad_s2 = 2000;
  policy.command_timeout_ms = 250;
  policy.output_period_ms = 100;
  Require(policy.IsValid(), "valid safety policy was rejected");

  ChassisSafetyAdapter adapter(policy);
  FakeChassisCommandSink sink;
  auto command = adapter.Evaluate(Ready(), 900);
  Require(command.stop_reason == ChassisStopReason::kNoCommand && !command.enabled,
          "missing command did not fail closed");

  std::string error;
  Require(adapter.Submit(ChassisVelocityRequest{3.0, -5.0}, Ready(), 1000, &error),
          "valid command was rejected");
  command = adapter.Evaluate(Ready(), 1000);
  Require(command.enabled && command.stop_reason == ChassisStopReason::kNone &&
              command.linear_velocity_mm_s == 100 && command.angular_velocity_mrad_s == -200,
          "first command did not apply slew limits");
  Require(sink.Send(command, &error), "fake chassis sink rejected safe command");

  command = adapter.Evaluate(Ready(), 1100);
  Require(command.linear_velocity_mm_s == 200 && command.angular_velocity_mrad_s == -400,
          "second command did not apply slew limits");
  Require(adapter.Submit(ChassisVelocityRequest{3.0, -5.0}, Ready(), 1250, &error),
          "refreshed command was rejected");
  command = adapter.Evaluate(Ready(), 1300);
  Require(command.linear_velocity_mm_s == 400 && command.angular_velocity_mrad_s == -800,
          "velocity clamp or slew limit mismatch");
  command = adapter.Evaluate(Ready(), 1400);
  Require(command.linear_velocity_mm_s == 400 && command.angular_velocity_mrad_s == -1000,
          "bounded command progression mismatch");
  command = adapter.Evaluate(Ready(), 1501);
  Require(command.stop_reason == ChassisStopReason::kCommandStale && !command.enabled &&
              command.linear_velocity_mm_s == 0 && command.angular_velocity_mrad_s == 0,
          "watchdog did not emit disabled zero command");

  Require(adapter.Submit(ChassisVelocityRequest{0.2, 0.1}, Ready(), 1600, &error),
          "recovery command was rejected");
  ChassisSafetyState state = Ready();
  state.emergency_stop = true;
  Require(adapter.Evaluate(state, 1600).stop_reason == ChassisStopReason::kEmergencyStop,
          "emergency stop did not dominate");
  Require(!adapter.Submit(ChassisVelocityRequest{0.2, 0.1}, state, 1610, &error),
          "command was cached while emergency stop was active");
  Require(adapter.Evaluate(Ready(), 1611).stop_reason == ChassisStopReason::kNoCommand,
          "emergency-stop release resumed an old command");
  state = Ready();
  state.chassis_fault = true;
  Require(adapter.Evaluate(state, 1620).stop_reason == ChassisStopReason::kChassisFault,
          "chassis fault did not stop output");
  state = Ready();
  state.peer_alive = false;
  Require(adapter.Evaluate(state, 1640).stop_reason == ChassisStopReason::kPeerUnavailable,
          "peer loss did not stop output");
  state = Ready();
  state.authority_granted = false;
  Require(adapter.Evaluate(state, 1660).stop_reason == ChassisStopReason::kAuthorityLost,
          "authority loss did not stop output");
  state = Ready();
  state.enabled = false;
  Require(adapter.Evaluate(state, 1680).stop_reason == ChassisStopReason::kDisabled,
          "disabled state did not stop output");

  const double nan = std::numeric_limits<double>::quiet_NaN();
  Require(!adapter.Submit(ChassisVelocityRequest{nan, 0.0}, Ready(), 1700, &error),
          "NaN command was accepted");
  Require(adapter.Evaluate(Ready(), 1700).stop_reason == ChassisStopReason::kInvalidCommand,
          "invalid command did not latch safe stop");
  adapter.RejectInvalidCommand(1750);
  Require(adapter.Evaluate(Ready(), 1750).stop_reason == ChassisStopReason::kInvalidCommand,
          "explicit adapter-boundary rejection did not latch safe stop");
  Require(adapter.Submit(ChassisVelocityRequest{0.1, 0.0}, Ready(), 1800, &error),
          "valid command did not clear invalid latch");
  Require(adapter.Evaluate(Ready(), 1799).stop_reason == ChassisStopReason::kClockRegression,
          "clock regression was not rejected");

  adapter.Reset(2000);
  command = adapter.Evaluate(Ready(), 2000);
  Require(command.stop_reason == ChassisStopReason::kNoCommand && !command.enabled,
          "reset reused a stale command");
  constexpr std::int64_t kLargeSteadyTime = 1000000000000LL;
  Require(adapter.Submit(ChassisVelocityRequest{0.1, 0.0}, Ready(), kLargeSteadyTime, &error),
          "command after a long idle interval was rejected");
  command = adapter.Evaluate(Ready(), kLargeSteadyTime);
  Require(command.enabled && command.linear_velocity_mm_s == 100,
          "long idle interval overflowed slew calculation");
  Require(
      std::string(cockpit::vehicle::ToString(ChassisStopReason::kCommandStale)) == "command_stale",
      "stop reason string mismatch");
  Require(sink.commands.size() == 1 && sink.commands.front().enabled,
          "fake chassis sink did not record command");

  std::cout << "chassis safety adapter tests passed\n";
  return 0;
}
