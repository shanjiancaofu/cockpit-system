#include "common/logging/Logger.h"
#include "common/runtime/ServiceRuntime.h"
#include "common/vehicle/VehicleState.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "vehicle-data-service");
  const auto& config = runtime.config();
  const int interval_ms = config.GetInt("vehicle.publish_interval_ms", 200);
  const int samples = runtime.args().GetInt("samples", 5);
  const bool forever = runtime.args().HasFlag("forever");

  int sequence = 0;
  do {
    const auto state = cockpit::vehicle::MakeMockVehicleState(sequence);
    const std::string json = state.ToJson();
    LOG_INFO("publish VehicleState " + json);
    std::cout << json << std::endl;
    ++sequence;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  } while (!runtime.ShouldStop() && (forever || sequence < samples));

  runtime.MarkStopped();
  return 0;
}
