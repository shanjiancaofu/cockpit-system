#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/library/driver/vehicle/socketcan_chassis_sink.h"
#include "cockpit/modules/vehicle/chassis_can_safety_state_source.h"
#include "cockpit/modules/vehicle/chassis_safety_adapter.h"

namespace cockpit::vehicle {

// VM-only integration owner. It is intentionally hard-limited to vcan0 and is
// not the production can0 runtime used for a real STM32.
class VcanChassisSafetyLoop final {
 public:
  static std::unique_ptr<VcanChassisSafetyLoop> OpenVcanOnly(const std::string& interface_name,
                                                             ChassisSafetyPolicy policy,
                                                             std::int64_t started_steady_ms,
                                                             std::string* error = nullptr);

  bool Submit(const ChassisVelocityRequest& request, const ChassisSafetyState& controls,
              std::int64_t steady_now_ms, std::string* error = nullptr);
  bool Step(const ChassisSafetyState& controls, std::int64_t steady_now_ms, int receive_timeout_ms,
            SafeChassisCommand* command, std::string* error = nullptr);
  const ChassisState& chassis_state() const {
    return state_source_.chassis_state();
  }

 private:
  VcanChassisSafetyLoop(can::SocketCan receive_socket,
                        std::unique_ptr<SocketCanChassisSink> command_sink,
                        ChassisSafetyPolicy policy, std::int64_t started_steady_ms);

  can::SocketCan receive_socket_;
  std::unique_ptr<SocketCanChassisSink> command_sink_;
  ChassisSafetyAdapter safety_adapter_;
  ChassisCanSafetyStateSource state_source_;
};

}  // namespace cockpit::vehicle
