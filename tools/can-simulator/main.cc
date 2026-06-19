#include "common/logging/Logger.h"
#include "common/runtime/ServiceRuntime.h"
#include "common/vehicle/VehicleState.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::string MockCanFrame(int sequence) {
  std::ostringstream out;
  out << "123#";
  for (int i = 0; i < 8; ++i) {
    out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << ((sequence + i) & 0xFF);
  }
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "can-simulator");
  const auto& config = runtime.config();

  const std::string can_if = config.GetString("can.interface", "vcan0");
  const int interval_ms = config.GetInt("can.simulator_interval_ms", 100);
  const int samples = runtime.args().GetInt("samples", 10);

  LOG_INFO("can-simulator started interface=" + can_if);
  for (int i = 0; i < samples && !runtime.ShouldStop(); ++i) {
    const std::string frame = MockCanFrame(i);
    LOG_DEBUG("emit CAN " + can_if + " " + frame);
    std::cout << can_if << ' ' << frame << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }
  runtime.MarkStopped();
  return 0;
}
