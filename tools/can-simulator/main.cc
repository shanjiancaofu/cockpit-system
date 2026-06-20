#include "common/can/can_frame.h"
#include "common/can/socket_can.h"
#include "common/logging/Logger.h"
#include "common/runtime/ServiceRuntime.h"

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

cockpit::can::CanFrame MakeMockCanFrame(int sequence) {
  std::array<std::uint8_t, cockpit::can::CanFrame::kMaxDataLength> data{};
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<std::uint8_t>((sequence + static_cast<int>(i)) & 0xFF);
  }
  return cockpit::can::CanFrame(0x123, data, static_cast<std::uint8_t>(data.size()));
}

}  // namespace

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "can-simulator");
  const auto& config = runtime.config();

  const std::string can_if = config.GetString("can.interface", "vcan0");
  const std::string backend =
      runtime.args().GetString("backend", config.GetString("can.simulator_backend", "stdout"));
  const int interval_ms = config.GetInt("can.simulator_interval_ms", 100);
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
    const auto frame = MakeMockCanFrame(i);
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
  runtime.MarkStopped();
  return 0;
}
