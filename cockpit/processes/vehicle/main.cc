#include <chrono>
#include <thread>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/driver/vehicle/vehicle_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "vehicle-data-service");
  cockpit::vehicle::VehicleRuntime vehicle;
  if (!vehicle.Start(runtime.config_path(), runtime.args().GetString("source", ""),
                     runtime.args().GetInt("samples", 5), runtime.args().HasFlag("forever"))) {
    runtime.MarkStopped();
    return 1;
  }
  while (!runtime.ShouldStop() && vehicle.Poll() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int result = runtime.ShouldStop() ? 0 : vehicle.Poll();
  vehicle.Stop();
  runtime.MarkStopped();
  return result;
}
