#include "can_simulator.h"

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/modules/can/can_frame.h"
#include "cockpit/modules/vehicle/vehicle_can_codec.h"
#include "cockpit/modules/vehicle/vehicle_state.h"

int cockpit::can_simulator::SimulateCan(const cockpit::runtime::ProcessRuntime& runtime) {
  const auto& config = runtime.config().hardware().can;

  const std::string& can_if = config.interface;
  const std::string backend = runtime.args().GetString("backend", config.simulator_backend);
  const int interval_ms = config.simulator_interval_ms;
  const int samples = runtime.args().GetInt("samples", 10);

  cockpit::can::SocketCan socket;
  if (backend == "socketcan") {
    std::string error;
    if (!socket.Open(can_if, &error)) {
      LOG_ERROR(error);
      return 1;
    }
  } else if (backend != "stdout") {
    LOG_ERROR("unsupported CAN simulator backend: " + backend);
    return 2;
  }

  LOG_INFO("can-simulator started interface=" + can_if + " backend=" + backend);
  for (int i = 0; i < samples && !runtime.ShouldStop(); ++i) {
    const auto frame =
        cockpit::vehicle::VehicleCanCodec::Encode(cockpit::vehicle::MakeMockVehicleState(i));
    if (backend == "socketcan") {
      std::string error;
      if (!socket.Send(frame, &error)) {
        LOG_ERROR(error);
        return 1;
      }
    }
    LOG_DEBUG("emit CAN " + can_if + " " + frame.ToString());
    std::cout << can_if << ' ' << frame.ToString() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }
  return 0;
}
