#include "cockpit/library/driver/vehicle/vcan_chassis_safety_loop.h"

#include <utility>

#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"

namespace cockpit::vehicle {

std::unique_ptr<VcanChassisSafetyLoop> VcanChassisSafetyLoop::OpenVcanOnly(
    const std::string& interface_name, ChassisSafetyPolicy policy, std::int64_t started_steady_ms,
    std::string* error) {
  if (interface_name != "vcan0" || !policy.IsValid() || started_steady_ms < 0) {
    if (error != nullptr) *error = "invalid VM-only vcan chassis safety configuration";
    return nullptr;
  }
  can::SocketCan receive_socket;
  if (!receive_socket.Open(interface_name, error)) return nullptr;
  auto command_sink = SocketCanChassisSink::OpenVcanOnly(interface_name, error);
  if (command_sink == nullptr) return nullptr;
  return std::unique_ptr<VcanChassisSafetyLoop>(new VcanChassisSafetyLoop(
      std::move(receive_socket), std::move(command_sink), policy, started_steady_ms));
}

VcanChassisSafetyLoop::VcanChassisSafetyLoop(can::SocketCan receive_socket,
                                             std::unique_ptr<SocketCanChassisSink> command_sink,
                                             ChassisSafetyPolicy policy,
                                             std::int64_t started_steady_ms)
    : receive_socket_(std::move(receive_socket)),
      command_sink_(std::move(command_sink)),
      safety_adapter_(policy),
      state_source_(started_steady_ms) {
}

bool VcanChassisSafetyLoop::Submit(const ChassisVelocityRequest& request,
                                   const ChassisSafetyState& controls, std::int64_t steady_now_ms,
                                   std::string* error) {
  return safety_adapter_.Submit(request, state_source_.Evaluate(controls, steady_now_ms),
                                steady_now_ms, error);
}

bool VcanChassisSafetyLoop::Step(const ChassisSafetyState& controls, std::int64_t steady_now_ms,
                                 int receive_timeout_ms, SafeChassisCommand* command,
                                 std::string* error) {
  if (command == nullptr || steady_now_ms < 0 || receive_timeout_ms < 0) {
    if (error != nullptr) *error = "invalid vcan chassis safety step arguments";
    return false;
  }

  for (int index = 0; index < 64; ++index) {
    can::SocketCanFrame socket_frame;
    const auto io_status =
        receive_socket_.Receive(&socket_frame, index == 0 ? receive_timeout_ms : 0, error);
    if (io_status == can::CanIoStatus::kTimeout) break;
    if (io_status != can::CanIoStatus::kOk) return false;
    std::int64_t frame_received_steady_ms = 0;
    if (!socket_frame.MapToLogicalSteadyMilliseconds(steady_now_ms, &frame_received_steady_ms)) {
      if (error != nullptr) *error = "invalid SocketCAN kernel RX timestamp";
      return false;
    }
    can::CanFrame frame;
    std::string decode_error;
    if (!can::FromSocketCanFrame(socket_frame, &frame, &decode_error)) {
      if (error != nullptr) *error = decode_error;
      return false;
    }
    if (frame.id() == ChassisCanCodec::kVelocityCommandId) continue;
    if ((frame.id() == ChassisCanCodec::kHeartbeatId ||
         frame.id() == ChassisCanCodec::kFaultStatusId) &&
        steady_now_ms - frame_received_steady_ms >= ChassisHeartbeatMonitor::kTimeoutMs) {
      continue;
    }
    static_cast<void>(state_source_.ProcessFrame(frame, frame_received_steady_ms));
  }

  *command =
      safety_adapter_.Evaluate(state_source_.Evaluate(controls, steady_now_ms), steady_now_ms);
  return command_sink_->Send(*command, error);
}

bool VcanChassisSafetyLoop::Stop(std::int64_t steady_now_ms, SafeChassisCommand* command,
                                 std::string* error) {
  if (command == nullptr || steady_now_ms < 0) {
    if (error != nullptr) *error = "invalid vcan chassis safety stop arguments";
    return false;
  }
  safety_adapter_.Reset(steady_now_ms);
  ChassisSafetyState stopped_state;
  stopped_state.authority_granted = true;
  stopped_state.peer_alive = true;
  *command = safety_adapter_.Evaluate(stopped_state, steady_now_ms);
  return command_sink_->Send(*command, error);
}

}  // namespace cockpit::vehicle
