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
#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/vehicle_can_codec.h"
#include "cockpit/modules/vehicle/vehicle_state.h"

int cockpit::can_simulator::SimulateCan(const cockpit::runtime::ProcessRuntime& runtime) {
  const auto& config = runtime.config().hardware().can;

  const std::string& can_if = config.interface;
  const std::string backend = runtime.args().GetString("backend", config.simulator_backend);
  const int interval_ms = config.simulator_interval_ms;
  const int samples = runtime.args().GetInt("samples", 10);
  const int fd_payload_size = runtime.args().GetInt("fd-payload-size", 0);
  if (fd_payload_size < 0 || fd_payload_size > 64) {
    LOG_ERROR("fd-payload-size must be between 0 and 64");
    return 2;
  }

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
  if (fd_payload_size > 0) {
    cockpit::can::SocketCanFrame diagnostic;
    diagnostic.id = 0x700U;
    diagnostic.fd = true;
    diagnostic.brs = runtime.args().HasFlag("brs");
    diagnostic.extended = runtime.args().HasFlag("extended");
    if (diagnostic.extended) {
      diagnostic.id = 0x18DAF110U;
    }
    diagnostic.length = static_cast<std::uint8_t>(fd_payload_size);
    for (int index = 0; index < fd_payload_size; ++index) {
      diagnostic.data[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(index);
    }
    if (backend == "socketcan") {
      std::string error;
      if (!socket.Send(diagnostic, &error)) {
        LOG_ERROR(error);
        return 1;
      }
    }
    std::cout << can_if << " diagnostic-canfd id=" << diagnostic.id << " length=" << fd_payload_size
              << " brs=" << (diagnostic.brs ? "true" : "false") << std::endl;
  }
  for (int i = 0; i < samples && !runtime.ShouldStop(); ++i) {
    const auto frame =
        cockpit::vehicle::VehicleCanCodec::Encode(cockpit::vehicle::MakeMockVehicleState(i));
    if (backend == "socketcan") {
      std::string error;
      if (!socket.Send(cockpit::can::ToSocketCanFrame(frame), &error)) {
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
